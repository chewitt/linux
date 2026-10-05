// SPDX-License-Identifier: GPL-2.0+
/*
 * Secure-world microcode loading for the Amlogic video decoder
 *
 * On newer SoCs, microcode ships as a signed package that only the secure
 * world can decrypt. A userspace loader passes the package to the vendor
 * video firmware trusted application once per boot. The secure monitor
 * then copies the image for each codec into the AMRISC on request.
 */

#include <linux/arm-smccc.h>

#include "amvdec_tee.h"

#define AMVDEC_SMC_LOAD_VIDEO_FW \
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL, ARM_SMCCC_SMC_32, \
			   ARM_SMCCC_OWNER_TRUSTED_OS, 15)

#define AMVDEC_TEE_ERROR_BAD_STATE	0xffff0007

int meson_amvdec_tee_load(struct amvdec_core *core, u32 format, u32 tee_core,
			  bool swap)
{
	struct arm_smccc_res res;

	arm_smccc_smc(AMVDEC_SMC_LOAD_VIDEO_FW, format, tee_core,
		      swap, 0, 0, 0, 0, &res);
	if (!((s32)res.a0 < 0))
		return 0;

	if ((u32)res.a0 == AMVDEC_TEE_ERROR_BAD_STATE)
		dev_err(core->dev_dec,
			"Secure world has no decoder microcode, is the firmware preloaded?\n");
	else if (swap)
		dev_dbg(core->dev_dec, "Secure load of microcode %#x failed: %#x\n",
			format, (u32)res.a0);
	else
		dev_err(core->dev_dec, "Secure load of microcode %#x failed: %#x\n",
			format, (u32)res.a0);

	return -EIO;
}
