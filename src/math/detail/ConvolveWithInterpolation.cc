// -*- LSST-C++ -*-

/*
 * LSST Data Management System
 * Copyright 2008, 2009, 2010 LSST Corporation.
 *
 * This product includes software developed by the
 * LSST Project (http://www.lsst.org/).
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the LSST License Statement and
 * the GNU General Public License along with this program.  If not,
 * see <http://www.lsstcorp.org/LegalNotices/>.
 */

/*
 * Definition of convolveWithInterpolation and helper functions declared in detail/ConvolveImage.h
 */
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <vector>
#include <iostream>

#include "lsst/pex/exceptions.h"
#include "lsst/log/Log.h"
#include "lsst/afw/image/MaskedImage.h"
#include "lsst/afw/math/Kernel.h"
#include "lsst/afw/geom.h"
#include "lsst/afw/math/detail/Convolve.h"

namespace pexExcept = lsst::pex::exceptions;
namespace afwGeom = lsst::afw::geom;
namespace afwImage = lsst::afw::image;
namespace afwMath = lsst::afw::math;
namespace mathDetail = lsst::afw::math::detail;

template <typename OutImageT, typename InImageT>
void mathDetail::convolveWithInterpolation(
        OutImageT &outImage,
        InImageT const &inImage,
        lsst::afw::math::Kernel const &kernel,
        lsst::afw::math::ConvolutionControl const &convolutionControl)
{
    if (outImage.getDimensions() != inImage.getDimensions()) {
        std::ostringstream os;
        os << "outImage dimensions = ( "
            << outImage.getWidth() << ", " << outImage.getHeight()
            << ") != (" << inImage.getWidth() << ", " << inImage.getHeight() << ") = inImage dimensions";
        throw LSST_EXCEPT(pexExcept::InvalidParameterError, os.str());
    }

    // compute region covering good area of output image
    afwGeom::Box2I fullBBox = afwGeom::Box2I(
        afwGeom::Point2I(0, 0),
        afwGeom::Extent2I(outImage.getWidth(), outImage.getHeight()));
    afwGeom::Box2I goodBBox = kernel.shrinkBBox(fullBBox);
    KernelImagesForRegion goodRegion(KernelImagesForRegion(
        kernel.clone(),
        goodBBox,
        inImage.getXY0(),
        convolutionControl.getDoNormalize()));
    LOGL_DEBUG("TRACE5.afw.math.convolve.convolveWithInterpolation",
        "convolveWithInterpolation: full bbox minimum=(%d, %d), extent=(%d, %d)",
            fullBBox.getMinX(), fullBBox.getMinY(),
            fullBBox.getWidth(), fullBBox.getHeight());
    LOGL_DEBUG("TRACE5.afw.math.convolve.convolveWithInterpolation",
        "convolveWithInterpolation: goodRegion bbox minimum=(%d, %d), extent=(%d, %d)",
            goodRegion.getBBox().getMinX(), goodRegion.getBBox().getMinY(),
            goodRegion.getBBox().getWidth(), goodRegion.getBBox().getHeight());

    // divide good region into subregions small enough to interpolate over
    int nx = 1 + (goodBBox.getWidth() / convolutionControl.getMaxInterpolationDistance());
    int ny = 1 + (goodBBox.getHeight() / convolutionControl.getMaxInterpolationDistance());
    LOGL_DEBUG("TRACE3.afw.math.convolve.convolveWithInterpolation",
        "convolveWithInterpolation: divide into %d x %d subregions", nx, ny);

    ConvolveWithInterpolationWorkingImages workingImages(kernel.getDimensions());
    RowOfKernelImagesForRegion regionRow(nx, ny);
    while (goodRegion.computeNextRow(regionRow)) {
        for (RowOfKernelImagesForRegion::ConstIterator rgnIter = regionRow.begin(), rgnEnd = regionRow.end();
            rgnIter != rgnEnd; ++rgnIter) {
            LOGL_DEBUG("TRACE5.afw.math.convolve.convolveWithInterpolation",
                "convolveWithInterpolation: bbox minimum=(%d, %d), extent=(%d, %d)",
                    (*rgnIter)->getBBox().getMinX(), (*rgnIter)->getBBox().getMinY(),
                    (*rgnIter)->getBBox().getWidth(), (*rgnIter)->getBBox().getHeight());
            convolveRegionWithInterpolation(outImage, inImage, **rgnIter, workingImages);
        }
    }
}

template <typename OutImageT, typename InImageT>
void mathDetail::convolveRegionWithInterpolation(
        OutImageT &outImage,
        InImageT const &inImage,
        KernelImagesForRegion const &region,
        ConvolveWithInterpolationWorkingImages &workingImages)
{
    typedef typename OutImageT::xy_locator OutLocator;
    typedef typename InImageT::const_xy_locator InConstLocator;
    typedef KernelImagesForRegion::Image KernelImage;
    typedef KernelImage::const_xy_locator KernelConstLocator;

    CONST_PTR(afwMath::Kernel) kernelPtr = region.getKernel();
    geom::Extent2I const kernelDimensions(kernelPtr->getDimensions());
    workingImages.leftImage.assign(*region.getImage(KernelImagesForRegion::BOTTOM_LEFT));
    workingImages.rightImage.assign(*region.getImage(KernelImagesForRegion::BOTTOM_RIGHT));
    workingImages.kernelImage.assign(workingImages.leftImage);

    afwGeom::Box2I const goodBBox = region.getBBox();
    afwGeom::Box2I const fullBBox = kernelPtr->growBBox(goodBBox);

    // top and right images are computed one beyond bbox boundary,
    // so the distance between edge images is bbox width/height pixels
    double xfrac = 1.0 / static_cast<double>(goodBBox.getWidth());
    double yfrac = 1.0 / static_cast<double>(goodBBox.getHeight());
    afwMath::scaledPlus(workingImages.leftDeltaImage,
         yfrac,  *region.getImage(KernelImagesForRegion::TOP_LEFT),
        -yfrac, workingImages.leftImage);
    afwMath::scaledPlus(workingImages.rightDeltaImage,
         yfrac, *region.getImage(KernelImagesForRegion::TOP_RIGHT),
        -yfrac, workingImages.rightImage);

    KernelConstLocator const kernelLocator = workingImages.kernelImage.xy_at(0, 0);

    // The loop is a bit odd for efficiency: the initial value of workingImages.kernelImage
    // and related kernel images are set when they are allocated,
    // so they are not computed in the loop until after the convolution; to save cpu cycles
    // they are not computed at all for the last iteration.
    InConstLocator inLocator = inImage.xy_at(fullBBox.getMinX(), fullBBox.getMinY());
    OutLocator outLocator = outImage.xy_at(goodBBox.getMinX(), goodBBox.getMinY());
    for (int j = 0; ; ) {
        auto inLocatorInitialPosition = inLocator;
        auto outLocatorInitialPosition = outLocator;
        afwMath::scaledPlus(
            workingImages.deltaImage, xfrac, workingImages.rightImage, -xfrac, workingImages.leftImage);
        for (int i = 0; ; ) {
            *outLocator = afwMath::convolveAtAPoint<OutImageT, InImageT>(
                inLocator, kernelLocator, kernelDimensions.getX(), kernelDimensions.getY());
            ++outLocator.x();
            ++inLocator.x();
            ++i;
            if (i >= goodBBox.getWidth()) {
                break;
            }
            workingImages.kernelImage += workingImages.deltaImage;
        }

        ++j;
        if (j >= goodBBox.getHeight()) {
            break;
        }
        workingImages.leftImage += workingImages.leftDeltaImage;
        workingImages.rightImage += workingImages.rightDeltaImage;
        workingImages.kernelImage.assign(workingImages.leftImage);

        // Workaround for DM-5822
        // Boost GIL locator won't decrement in x-dimension for some strange and still
        // not understood reason. So instead store position at start of previous row,
        // reset and move down.
        inLocator = inLocatorInitialPosition;
        outLocator = outLocatorInitialPosition;
        ++inLocator.y();
        ++outLocator.y();
    }
}

/*
 * Explicit instantiation
 */
/// @cond
#define IMAGE(PIXTYPE) afwImage::Image<PIXTYPE>
#define MASKEDIMAGE(PIXTYPE) afwImage::MaskedImage<PIXTYPE, afwImage::MaskPixel, afwImage::VariancePixel>
#define NL /* */
// Instantiate Image or MaskedImage versions
#define INSTANTIATE_IM_OR_MI(IMGMACRO, OUTPIXTYPE, INPIXTYPE) \
    template void mathDetail::convolveWithInterpolation( \
        IMGMACRO(OUTPIXTYPE)&, IMGMACRO(INPIXTYPE) const&, afwMath::Kernel const&, \
            afwMath::ConvolutionControl const&); NL \
    template void mathDetail::convolveRegionWithInterpolation( \
        IMGMACRO(OUTPIXTYPE)&, IMGMACRO(INPIXTYPE) const&, KernelImagesForRegion const&, \
        ConvolveWithInterpolationWorkingImages&);
// Instantiate both Image and MaskedImage versions
#define INSTANTIATE(OUTPIXTYPE, INPIXTYPE) \
    INSTANTIATE_IM_OR_MI(IMAGE,       OUTPIXTYPE, INPIXTYPE) \
    INSTANTIATE_IM_OR_MI(MASKEDIMAGE, OUTPIXTYPE, INPIXTYPE)

INSTANTIATE(double, double)
INSTANTIATE(double, float)
INSTANTIATE(double, int)
INSTANTIATE(double, std::uint16_t)
INSTANTIATE(float, float)
INSTANTIATE(float, int)
INSTANTIATE(float, std::uint16_t)
INSTANTIATE(int, int)
INSTANTIATE(std::uint16_t, std::uint16_t)
/// @endcond
