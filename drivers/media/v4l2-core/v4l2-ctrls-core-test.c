// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * KUnit tests for HEVC/AV1 tile-count validation in std_validate_compound().
 * #included at the end of v4l2-ctrls-core.c to reach the static helper.
 */

#include <kunit/test.h>

static int call_validate_compound(enum v4l2_ctrl_type type, void *payload,
				  u32 elem_size)
{
	struct v4l2_ctrl ctrl = {
		.type = type,
		.elem_size = elem_size,
	};
	union v4l2_ctrl_ptr ptr = { .p = payload };

	return std_validate_compound(&ctrl, 0, ptr);
}

/* HEVC PPS: num_tile_columns_minus1 / num_tile_rows_minus1 bounds. */
static void v4l2_ctrls_hevc_pps_tile_cols(struct kunit *test)
{
	struct v4l2_ctrl_hevc_pps *pps;

	pps = kunit_kzalloc(test, sizeof(*pps), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, pps);

	pps->flags = V4L2_HEVC_PPS_FLAG_TILES_ENABLED;

	/* In range: count == array capacity (minus1 == capacity - 1). */
	pps->num_tile_columns_minus1 = ARRAY_SIZE(pps->column_width_minus1) - 1;
	pps->num_tile_rows_minus1 = ARRAY_SIZE(pps->row_height_minus1) - 1;
	KUNIT_EXPECT_EQ(test,
			call_validate_compound(V4L2_CTRL_TYPE_HEVC_PPS, pps,
					       sizeof(*pps)),
			0);

	/* Out of range: one past the column array. */
	pps->num_tile_columns_minus1 = ARRAY_SIZE(pps->column_width_minus1);
	pps->num_tile_rows_minus1 = 0;
	KUNIT_EXPECT_EQ(test,
			call_validate_compound(V4L2_CTRL_TYPE_HEVC_PPS, pps,
					       sizeof(*pps)),
			-EINVAL);

	/* Out of range: maximal attacker value. */
	pps->num_tile_columns_minus1 = 0xff;
	pps->num_tile_rows_minus1 = 0xff;
	KUNIT_EXPECT_EQ(test,
			call_validate_compound(V4L2_CTRL_TYPE_HEVC_PPS, pps,
					       sizeof(*pps)),
			-EINVAL);
}

/* AV1 frame: tile_cols / tile_rows upper bounds. */
static void v4l2_ctrls_av1_frame_tile(struct kunit *test)
{
	struct v4l2_ctrl_av1_frame *f;

	f = kunit_kzalloc(test, sizeof(*f), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, f);

	/*
	 * Benign control: a zero-initialised frame (tile_cols == 0) must
	 * still pass. Userspace and v4l2-compliance set the zeroed default,
	 * and the divisor that a zero tile_cols would feed is guarded in the
	 * consuming driver rather than rejected here.
	 */
	KUNIT_EXPECT_EQ(test,
			call_validate_compound(V4L2_CTRL_TYPE_AV1_FRAME, f,
					       sizeof(*f)),
			0);

	/* In range: a 1x1 tiling. */
	f->tile_info.tile_cols = 1;
	f->tile_info.tile_rows = 1;
	KUNIT_EXPECT_EQ(test,
			call_validate_compound(V4L2_CTRL_TYPE_AV1_FRAME, f,
					       sizeof(*f)),
			0);

	/*
	 * In range: total tiles == V4L2_AV1_MAX_TILE_COUNT with each
	 * dimension at or below its per-dimension maximum (64 * 8 == 512).
	 */
	f->tile_info.tile_cols = V4L2_AV1_MAX_TILE_COLS;
	f->tile_info.tile_rows = V4L2_AV1_MAX_TILE_COUNT / V4L2_AV1_MAX_TILE_COLS;
	KUNIT_EXPECT_EQ(test,
			call_validate_compound(V4L2_CTRL_TYPE_AV1_FRAME, f,
					       sizeof(*f)),
			0);

	/*
	 * Out of range: each dimension is legal on its own but the product
	 * exceeds V4L2_AV1_MAX_TILE_COUNT (64 * 64 == 4096 > 512), which would
	 * overflow the per-tile descriptor buffers.
	 */
	f->tile_info.tile_cols = V4L2_AV1_MAX_TILE_COLS;
	f->tile_info.tile_rows = V4L2_AV1_MAX_TILE_ROWS;
	KUNIT_EXPECT_EQ(test,
			call_validate_compound(V4L2_CTRL_TYPE_AV1_FRAME, f,
					       sizeof(*f)),
			-EINVAL);

	/* Out of range: tile_cols past the array. */
	f->tile_info.tile_cols = V4L2_AV1_MAX_TILE_COLS + 1;
	f->tile_info.tile_rows = 1;
	KUNIT_EXPECT_EQ(test,
			call_validate_compound(V4L2_CTRL_TYPE_AV1_FRAME, f,
					       sizeof(*f)),
			-EINVAL);

	/* Out of range: maximal attacker value. */
	f->tile_info.tile_cols = 0xff;
	f->tile_info.tile_rows = 0xff;
	KUNIT_EXPECT_EQ(test,
			call_validate_compound(V4L2_CTRL_TYPE_AV1_FRAME, f,
					       sizeof(*f)),
			-EINVAL);

	/* Out of range: tile_rows past the array. */
	f->tile_info.tile_cols = 1;
	f->tile_info.tile_rows = V4L2_AV1_MAX_TILE_ROWS + 1;
	KUNIT_EXPECT_EQ(test,
			call_validate_compound(V4L2_CTRL_TYPE_AV1_FRAME, f,
					       sizeof(*f)),
			-EINVAL);
}

static struct kunit_case v4l2_ctrls_test_cases[] = {
	KUNIT_CASE(v4l2_ctrls_hevc_pps_tile_cols),
	KUNIT_CASE(v4l2_ctrls_av1_frame_tile),
	{}
};

static struct kunit_suite v4l2_ctrls_test_suite = {
	.name = "v4l2-ctrls-compound",
	.test_cases = v4l2_ctrls_test_cases,
};

kunit_test_suite(v4l2_ctrls_test_suite);

MODULE_DESCRIPTION("KUnit tests for V4L2 stateless-codec compound control validation");
MODULE_LICENSE("GPL");
