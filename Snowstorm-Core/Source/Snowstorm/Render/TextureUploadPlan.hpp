#pragma once

namespace Snowstorm
{
	enum class UploadLayout
	{
		TransferDst,
		Final,
	};

	// Where the copy-only mip upload (SetMipData) changes layout. A transfer-only queue family cannot take an
	// image to SHADER_READ_ONLY (that layout is graphics/compute only), so with a dedicated transfer queue the
	// release hands the image over still in TRANSFER_DST and the graphics queue's acquire does the change.
	// With one shared family the release itself is a legal DST -> Final.
	struct MipUploadPlan
	{
		UploadLayout ReleaseNewLayout = UploadLayout::Final;
		bool AcquireOnGraphics = false;
		UploadLayout AcquireNewLayout = UploadLayout::Final;
	};

	constexpr MipUploadPlan PlanMipUpload(const bool dedicatedTransferQueue, const bool finalIsTransferDst)
	{
		MipUploadPlan p;
		p.AcquireOnGraphics = dedicatedTransferQueue;
		p.ReleaseNewLayout = dedicatedTransferQueue && !finalIsTransferDst ? UploadLayout::TransferDst : UploadLayout::Final;
		return p;
	}
}
