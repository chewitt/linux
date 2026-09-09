// SPDX-License-Identifier: GPL-2.0+
/* VC-1 Annex G preprocessing for the legacy Amlogic microcode. */
#include <linux/errno.h>
#include <linux/string.h>

#include "codec_vc1_parser.h"

static u32 vc1_bits(const u8 *data, u32 *pos, u32 count)
{
	u32 value = 0;

	while (count--) {
		value = (value << 1) | ((data[*pos / 8] >> (7 - *pos % 8)) & 1);
		++*pos;
	}
	return value;
}

static void vc1_put_bits(u8 *data, u32 *pos, u32 count, u32 value)
{
	while (count--) {
		data[*pos / 8] |= ((value >> count) & 1) << (7 - *pos % 8);
		++*pos;
	}
}

static u32 vc1_unescape(const u8 *src, u32 len, u8 *dst, u32 capacity)
{
	u32 i, n = 0, zeros = 0;

	for (i = 0; i < len && n < capacity; ++i) {
		if (zeros == 2 && src[i] == 3 && i + 1 < len && src[i + 1] <= 3) {
			zeros = 0;
			continue;
		}
		dst[n++] = src[i];
		zeros = src[i] == 0 ? zeros + 1 : 0;
		if (zeros > 2)
			zeros = 2;
	}
	return n;
}

int vc1_sequence_header(const u8 *data, u32 length, u32 *width, u32 *height)
{
	u8 header[6];
	u32 pos = 16;

	if (vc1_unescape(data, length, header, sizeof(header)) < sizeof(header) ||
	    header[0] >> 6 != 3 || ((header[0] >> 1) & 3) != 1)
		return -EINVAL;
	*width = (vc1_bits(header, &pos, 12) + 1) * 2;
	*height = (vc1_bits(header, &pos, 12) + 1) * 2;
	if (*width < 16 || *width > 1920 || *height < 16 || *height > 1088)
		return -EINVAL;
	return 0;
}

static u32 vc1_next_bdu(const u8 *data, u32 from, u32 len)
{
	u32 i;

	for (i = from; i + 3 < len; ++i)
		if (!data[i] && !data[i + 1] && data[i + 2] == 1)
			return i;
	return len;
}

/*
 * G12 VC-1 microcode erratum (observed with g12a_vc1 SHA-256 e5aacc59...):
 * it drops PTYPE=1111 pictures without advancing its references. B
 * pictures between the skipped anchors can then read an uninitialised frame.
 * Expand a progressive skipped P picture into a P picture with zero motion
 * and every macroblock skipped. All reconstruction still runs on hardware.
 *
 * Inverted ROWSKIP encodes the all-one SKIPMB plane in one bit per row.
 * PQINDEX=9 avoids HALFQP, 1MV uses zero spatially predicted motion, DQUANT
 * is disabled (or uses equal quantizers), and there are no coded residuals.
 * Preserve TFCNTR and RPTFRM from the original picture.
 *
 * This is a bounded firmware workaround, not a general picture encoder:
 * only progressive skipped pictures are expanded, at most 48 bytes are
 * generated, and unsupported pan-scan syntax fails rather than being lost.
 * Testing SM1 microcode with the newer eight-buffer protocol did not yield
 * a working replacement. Keep this until native reference handling is
 * verified with an alternative firmware, including skipped anchors and EOS.
 */
static int vc1_expand_skip(const struct vc1_stream *s, const u8 *src,
			   u32 len, u8 *dst, u32 height)
{
	u8 header[16], picture[32] = { 0 };
	u32 n, in = 4, out = 0, i, zeros = 0, bytes;

	n = vc1_unescape(src, len, header, sizeof(header));
	if (!n || (header[0] >> 4) != 15)
		return 0;
	if (s->panscan || !height || height > 1088)
		return -EOPNOTSUPP;
	if (n * 8 < 4U + s->tfcntr * 8 + s->broadcast * 2)
		return -EINVAL;

	vc1_put_bits(picture, &out, 1, 0); /* PTYPE: P */
	if (s->tfcntr)
		vc1_put_bits(picture, &out, 8, vc1_bits(header, &in, 8));
	if (s->broadcast)
		vc1_put_bits(picture, &out, 2, vc1_bits(header, &in, 2));
	vc1_put_bits(picture, &out, 1, 0); /* RNDCTRL */
	if (s->finterp)
		vc1_put_bits(picture, &out, 1, 0);
	vc1_put_bits(picture, &out, 5, 9); /* PQINDEX */
	if (s->quantizer == 1)
		vc1_put_bits(picture, &out, 1, 1); /* PQUANTIZER */
	if (s->postproc)
		vc1_put_bits(picture, &out, 2, 0);
	if (s->extended_mv)
		vc1_put_bits(picture, &out, 1, 0); /* MVRANGE */
	vc1_put_bits(picture, &out, 1, 1); /* MVMODE: 1MV */
	vc1_put_bits(picture, &out, 1, 1); /* INVERT */
	vc1_put_bits(picture, &out, 3, 2); /* IMODE: ROWSKIP */
	for (i = 0; i < (height + 15) / 16; ++i)
		vc1_put_bits(picture, &out, 1, 0);
	vc1_put_bits(picture, &out, 4, 0); /* MVTAB, CBPTAB */
	if (s->dquant == 2) {
		vc1_put_bits(picture, &out, 3, 7); /* explicit ALTPQUANT */
		vc1_put_bits(picture, &out, 5, 9);
	} else if (s->dquant) {
		vc1_put_bits(picture, &out, 1, 0); /* DQUANTFRM */
	}
	if (s->vstransform)
		vc1_put_bits(picture, &out, 3, 4); /* TTMBF=1, TTFRM=8x8 */
	vc1_put_bits(picture, &out, 2, 0); /* TRANSACFRM, TRANSDCTAB */

	bytes = (out + 7) / 8;
	for (i = 0, n = 0; i < bytes; ++i) {
		if (zeros == 2 && picture[i] <= 3) {
			dst[n++] = 3;
			zeros = 0;
		}
		dst[n++] = picture[i];
		zeros = picture[i] == 0 ? zeros + 1 : 0;
	}
	return n;
}

int vc1_flush_picture(const struct vc1_stream *stream, u8 *data, u32 height)
{
	const u8 skipped[] = { 0xf0, 0, 0 };

	if (!stream->sequence_valid || !stream->entry_valid || stream->interlace)
		return -EOPNOTSUPP;
	return vc1_expand_skip(stream, skipped, sizeof(skipped), data, height);
}

/* Bounded header reads used only to locate a raw picture after ASF codec
 * private data. FFmpeg concatenates that data and its first frame in one
 * OUTPUT buffer, without retaining the boundary in V4L2 metadata.
 */
static u32 vc1_header_bits(const u8 *data, u32 n, u32 *pos, u32 count)
{
	if (*pos > n * 8 || count > n * 8 - *pos) {
		*pos = n * 8 + 1;
		return 0;
	}
	return vc1_bits(data, pos, count);
}

static int vc1_sequence_buckets(const u8 *data, u32 len)
{
	u8 header[160];
	u32 n = vc1_unescape(data, len, header, sizeof(header));
	u32 pos = 46, buckets = 0;

	if (vc1_header_bits(header, n, &pos, 1)) { /* DISPLAY_EXT */
		pos += 28;
		if (vc1_header_bits(header, n, &pos, 1) &&
		    vc1_header_bits(header, n, &pos, 4) == 15)
			pos += 16;
		if (vc1_header_bits(header, n, &pos, 1)) { /* FRAMERATE_FLAG */
			if (vc1_header_bits(header, n, &pos, 1))
				pos += 16;
			else
				pos += 12;
		}
		if (vc1_header_bits(header, n, &pos, 1))
			pos += 24; /* COLOR_FORMAT */
	}
	if (vc1_header_bits(header, n, &pos, 1)) {
		buckets = vc1_header_bits(header, n, &pos, 5);
		pos += 8 + 32 * buckets;
	}
	return pos <= n * 8 ? (int)buckets : -EINVAL;
}

static int vc1_entry_size(const u8 *data, u32 len, u32 buckets,
			  u32 width, u32 height)
{
	u8 header[48];
	u32 n = vc1_unescape(data, len, header, sizeof(header));
	u32 pos = 6, extended_mv, w, h, bytes, i, zeros = 0;

	extended_mv = vc1_header_bits(header, n, &pos, 1);
	pos = 13 + 8 * buckets;
	if (vc1_header_bits(header, n, &pos, 1)) {
		w = (vc1_header_bits(header, n, &pos, 12) + 1) * 2;
		h = (vc1_header_bits(header, n, &pos, 12) + 1) * 2;
		if (w != width || h != height)
			return -EOPNOTSUPP;
	}
	pos += extended_mv;
	if (vc1_header_bits(header, n, &pos, 1))
		pos += 3;
	if (vc1_header_bits(header, n, &pos, 1))
		pos += 3;
	/* Annex G headers end in a one bit followed by byte-alignment zeros. */
	if (vc1_header_bits(header, n, &pos, 1) != 1)
		return -EINVAL;
	while (pos % 8 && pos <= n * 8)
		if (vc1_header_bits(header, n, &pos, 1))
			return -EINVAL;
	if (pos > n * 8)
		return -EINVAL;
	bytes = pos / 8;
	/* Map the parsed RBSP length back to the escaped input length. */
	for (i = 0; i < len && bytes; ++i) {
		if (zeros == 2 && data[i] == 3 && i + 1 < len && data[i + 1] <= 3) {
			zeros = 0;
			continue;
		}
		--bytes;
		zeros = data[i] ? 0 : zeros < 2 ? zeros + 1 : 2;
	}
	return bytes ? -EINVAL : (int)i;
}

int vc1_prepare_stream(struct vc1_stream *s, u8 *data, u32 *length,
		       u32 capacity, u32 width, u32 height)
{
	u32 start, end, pos, n, w, h, len = *length;
	u8 header[8], replacement[48];
	int size, buckets = -EINVAL;
	bool asf_headers;

	if (len > capacity)
		return -EINVAL;
	/* ASF codec private data has a one-byte prefix before its sequence BDU. */
	asf_headers = len >= 5 && !data[1] && !data[2] &&
		      data[3] == 1 && data[4] == 0x0f;
	/* Some demuxers supply raw frame packets despite selecting Annex G.
	 * Normalize once, before scanning BDUs or expanding skipped pictures.
	 * Sequence/entry headers and already framed packets pass through.
	 */
	if (len && !asf_headers && (len < 3 || data[0] || data[1] || data[2] != 1)) {
		if (capacity - len < 4)
			return -ENOSPC;
		memmove(data + 4, data, len);
		data[0] = 0;
		data[1] = 0;
		data[2] = 1;
		data[3] = 0x0d;
		len += 4;
	}
	for (start = vc1_next_bdu(data, 0, len); start < len; start = end) {
		end = vc1_next_bdu(data, start + 4, len);
		n = vc1_unescape(data + start + 4, end - start - 4,
				 header, sizeof(header));
		switch (data[start + 3]) {
		case 0x0f:
			s->sequence_valid = false;
			s->entry_valid = false;
			if (vc1_sequence_header(data + start + 4, end - start - 4, &w, &h))
				return -EINVAL;
			/* Reject a new size before the microcode can write to old canvases. */
			if (w != width || h != height)
				return -EOPNOTSUPP;
			pos = 15;
			s->postproc = vc1_bits(header, &pos, 1);
			pos = 40;
			s->broadcast = vc1_bits(header, &pos, 1);
			s->interlace = vc1_bits(header, &pos, 1);
			s->tfcntr = vc1_bits(header, &pos, 1);
			s->finterp = vc1_bits(header, &pos, 1);
			if (asf_headers)
				buckets = vc1_sequence_buckets(data + start + 4, end - start - 4);
			s->sequence_valid = true;
			break;
		case 0x0e:
			if (asf_headers && end == len) {
				if (buckets < 0)
					return buckets;
				size = vc1_entry_size(data + start + 4, end - start - 4,
						      buckets, width, height);
				if (size < 0)
					return size;
				pos = start + 4 + size;
				if (pos < len) {
					if (capacity - len < 4)
						return -ENOSPC;
					memmove(data + pos + 4, data + pos, len - pos);
					memcpy(data + pos, "\x00\x00\x01\x0d", 4);
					len += 4;
					end = pos;
				}
			}
			if (n < 2)
				return -EINVAL;
			pos = 2;
			s->panscan = vc1_bits(header, &pos, 1);
			pos = 6;
			s->extended_mv = vc1_bits(header, &pos, 1);
			s->dquant = vc1_bits(header, &pos, 2);
			s->vstransform = vc1_bits(header, &pos, 1);
			++pos; /* OVERLAP */
			s->quantizer = vc1_bits(header, &pos, 2);
			s->entry_valid = true;
			break;
		case 0x0d:
			if (!s->sequence_valid || !s->entry_valid || s->interlace)
				break;
			size = vc1_expand_skip(s, data + start + 4,
					       end - start - 4, replacement, height);
			if (size < 0)
				return size;
			if (!size)
				break;
			n = len - (end - start - 4);
			if ((u32)size > capacity - n)
				return -ENOSPC;
			memmove(data + start + 4 + size, data + end, len - end);
			memcpy(data + start + 4, replacement, size);
			len = n + size;
			end = start + 4 + size;
			break;
		}
	}
	*length = len;
	return 0;
}
