// -*- LSST-C++ -*-
/**
 * @file
 *
 * @brief Definition of functions declared in ConvolveImage.h
 *
 * This file is meant to be included by lsst/afw/math/KernelFunctions.h
 *
 * @todo
 * * Speed up convolution
 *
 * @note: the convolution and convolveAtAPoint functions assume that data in a row is contiguous,
 * both in the input image and in the kernel. This is enforced by afw.
 *
 * @author Russell Owen
 *
 * @ingroup afw
 */
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <vector>
#include <string>

#include "boost/format.hpp"

#include "lsst/pex/logging/Trace.h"
#include "lsst/afw/image/ImageUtils.h"
#include "lsst/afw/image/MaskedImage.h"
#include "lsst/afw/math.h"

// if true, ignore kernel pixels that have value 0 when convolving (only affects propagation of mask bits)

namespace pexExcept = lsst::pex::exceptions;
namespace afwImage = lsst::afw::image;
namespace afwMath = lsst::afw::math;


namespace {
    /**
     * \brief Compute the portion of one pixel of a convolved image from one row of a kernel.
     *
     * The new component is added to the existing component. When you handle all rows
     * of the kernel the output pixel is complete.
     *
     * The pixel computed is at position inImageIter + kernel center.
     *
     * \todo The first argument should be a pixel, not a pixel iterator,
     *  but I can't figure out how to do it and have the function found
     */
    template <typename OutImageT, typename InImageT>
    inline void convolveOneKernelRow(
        typename OutImageT::x_iterator outImageIter,    ///< component of output pixel as x iterator
//        typename OutImageT::SinglePixel &outPixel,   ///< component of output pixel
        typename InImageT::const_x_iterator inImageIter,    ///< start of input image that overlaps kernel row
        typename afwImage::Image<afwMath::Kernel::PixelT>::const_x_iterator kernelIter, ///< start of kernel row
        int kWidth  ///< width of kernel
    ) {

        // to avoid slowdown due to multiple accesses to outPixel,
        // copy outPixel, increment the copy in the loop, then copy it back
        typename OutImageT::SinglePixel locOutPixel(*outImageIter);
        for (int x = 0; x != kWidth; ++x, ++inImageIter, ++kernelIter) {
            typename afwMath::Kernel::Pixel const kVal = kernelIter[0];
            if (kVal != 0) {
                locOutPixel += *inImageIter*kVal;
            }
        }
        
        *outImageIter = locOutPixel;
    };

    /*
     * Private functions to copy the border of an image
     *
     * copyRegion gets a bit complicated --- is it really worth it for private functions?
     */
    /*
     * Copy a rectangular region from one Image to another
     */
    template<typename OutImageT, typename InImageT>
    inline void copyRegion(OutImageT &outImage,     // destination Image
                           InImageT const &inImage, // source Image
                           afwImage::BBox const &region, // region to copy
                           int,
                           afwImage::detail::Image_tag
                          ) {
        OutImageT outPatch(outImage, region); 
        InImageT inPatch(inImage, region);
        outPatch <<= OutImageT(inPatch, true);
    }
    // Specialization when the two types are the same
    template<typename InImageT>
    inline void copyRegion(InImageT &outImage,     // destination Image
                           InImageT const &inImage, // source Image
                           afwImage::BBox const &region, // region to copy
                           int,
                           afwImage::detail::Image_tag
                          ) {
        InImageT outPatch(outImage, region); 
        InImageT inPatch(inImage, region);
        outPatch <<= inPatch;
    }
    
    /*
     * Copy a rectangular region from one MaskedImage to another, setting the bits in orMask
     */
    template<typename OutImageT, typename InImageT>
    inline void copyRegion(OutImageT &outImage,     // destination Image
                           InImageT const &inImage, // source Image
                           afwImage::BBox const &region, // region to copy
                           int orMask,                           // data to | into the mask pixels
                           afwImage::detail::MaskedImage_tag
                          ) {
        OutImageT outPatch(outImage, region); 
        InImageT inPatch(inImage, region);
        outPatch <<= OutImageT(inPatch, true);
        *outPatch.getMask() |= orMask;
    }
    // Specialization when the two types are the same
    template<typename InImageT>
    inline void copyRegion(InImageT &outImage,      // destination Image
                           InImageT const &inImage, // source Image
                           afwImage::BBox const &region, // region to copy
                           int orMask,                           // data to | into the mask pixels
                           afwImage::detail::MaskedImage_tag
                          ) {
        InImageT outPatch(outImage, region);
        InImageT inPatch(inImage, region);
        outPatch <<= inPatch;
        *outPatch.getMask() |= orMask;
    }
    
    
    template <typename OutImageT, typename InImageT>
    inline void copyBorder(
        OutImageT& convolvedImage,                           ///< convolved image
        InImageT const& inImage,                             ///< image to convolve
        afwMath::Kernel const &kernel,               ///< convolution kernel
        int edgeBit                         ///< bit to set to indicate border pixel;  if negative then no bit is set
    ) {
        const unsigned int imWidth = inImage.getWidth();
        const unsigned int imHeight = inImage.getHeight();
        const unsigned int kWidth = kernel.getWidth();
        const unsigned int kHeight = kernel.getHeight();
        const unsigned int kCtrX = kernel.getCtrX();
        const unsigned int kCtrY = kernel.getCtrY();
    
        const int edgeBitMask = (edgeBit < 0) ? 0 : (1 << edgeBit);
    
        using afwImage::BBox;
        using afwImage::PointI;
        BBox bottomEdge(PointI(0, 0), imWidth, kCtrY);
        copyRegion(convolvedImage, inImage, bottomEdge, edgeBitMask,
                    typename afwImage::detail::image_traits<OutImageT>::image_category());
        
        int numHeight = kHeight - (1 + kCtrY);
        BBox topEdge(PointI(0, imHeight - numHeight), imWidth, numHeight);
        copyRegion(convolvedImage, inImage, topEdge, edgeBitMask,
                    typename afwImage::detail::image_traits<OutImageT>::image_category());
        
        BBox leftEdge(PointI(0, kCtrY), kCtrX, imHeight + 1 - kHeight);
        copyRegion(convolvedImage, inImage, leftEdge, edgeBitMask,
                    typename afwImage::detail::image_traits<OutImageT>::image_category());
        
        int numWidth = kWidth - (1 + kCtrX);
        BBox rightEdge(PointI(imWidth - numWidth, kCtrY), numWidth, imHeight + 1 - kHeight);
        copyRegion(convolvedImage, inImage, rightEdge, edgeBitMask,
                    typename afwImage::detail::image_traits<OutImageT>::image_category());
    }
}   // anonymous namespace


/**
 * @brief Low-level convolution function that does not set edge pixels.
 *
 * convolvedImage must be the same size as inImage.
 * convolvedImage has a border in which the output pixels are not set. This border has size:
 * * kernel.getCtrX/Y() along the left/bottom edge
 * * kernel.getWidth/Height() - 1 - kernel.getCtrX/Y() along the right/top edge
 *
 * @throw lsst::pex::exceptions::InvalidParameterException if convolvedImage is not the same size as inImage.
 * @throw lsst::pex::exceptions::InvalidParameterException if inImage is smaller (in colums or rows) than kernel.
 *
 * @ingroup afw
 */
template <typename OutImageT, typename InImageT>
void afwMath::basicConvolve(
    OutImageT &convolvedImage,      ///< convolved image
    InImageT const& inImage,        ///< image to convolve
    afwMath::Kernel const& kernel,  ///< convolution kernel
    bool doNormalize                ///< if True, normalize the kernel, else use "as is"
) {
    typedef typename afwMath::Kernel::PixelT KernelPixelT;
    typedef afwImage::Image<KernelPixelT> KernelImageT;

    typedef typename KernelImageT::const_x_iterator KernelXIterator;
    typedef typename KernelImageT::const_xy_locator KernelXYLocator;
    typedef typename InImageT::const_x_iterator InXYIterator;
    typedef typename InImageT::const_xy_locator InXYLocator;
    typedef typename OutImageT::x_iterator OutXIterator;

    // Because convolve isn't a method of Kernel we can't always use Kernel's vtbl to dynamically
    // dispatch the correct version of basicConvolve. The case that fails is convolving with a kernel
    // obtained from a shared_ptr to a Kernel (base class), e.g. as used in linearCombinationKernel.
    if (dynamic_cast<afwMath::DeltaFunctionKernel const*>(&kernel) != NULL) {
        afwMath::basicConvolve(convolvedImage, inImage,
            *dynamic_cast<afwMath::DeltaFunctionKernel const*>(&kernel),
            doNormalize);
        return;
    } else if (dynamic_cast<afwMath::SeparableKernel const*>(&kernel) != NULL) {
        afwMath::basicConvolve(convolvedImage, inImage,
            *dynamic_cast<afwMath::SeparableKernel const*>(&kernel),
            doNormalize);
        return;
    }
    // OK, use general (and slower) form

    if (convolvedImage.getDimensions() != inImage.getDimensions()) {
        throw LSST_EXCEPT(pexExcept::InvalidParameterException, "convolvedImage not the same size as inImage");
    }
    if (inImage.getDimensions() < kernel.getDimensions()) {
        throw LSST_EXCEPT(pexExcept::InvalidParameterException,"inImage smaller than kernel in columns and/or rows");
    }
    
    int const inImageWidth = inImage.getWidth();
    int const inImageHeight = inImage.getHeight();
    int const kWidth = kernel.getWidth();
    int const kHeight = kernel.getHeight();
    int const cnvWidth = inImageWidth + 1 - kernel.getWidth();
    int const cnvHeight = inImageHeight + 1 - kernel.getHeight();
    int const cnvStartX = kernel.getCtrX();
    int const cnvStartY = kernel.getCtrY();
    int const cnvEndX = cnvStartX + cnvWidth;  // end index + 1
    int const cnvEndY = cnvStartY + cnvHeight; // end index + 1

    KernelImageT kernelImage(kernel.getDimensions()); // the kernel at a point

    if (kernel.isSpatiallyVarying()) {
        lsst::pex::logging::TTrace<3>("lsst.afw.kernel.convolve", "kernel is spatially varying");

        for (int cnvY = cnvStartY; cnvY != cnvEndY; ++cnvY) {
            double const rowPos = afwImage::indexToPosition(cnvY);
            
            InXYLocator  inImLoc =  inImage.xy_at(0, cnvY - cnvStartY);
            OutXIterator cnvXIter = convolvedImage.x_at(cnvStartX, cnvY);
            for (int cnvX = cnvStartX; cnvX != cnvEndX; ++cnvX, ++inImLoc.x(), ++cnvXIter) {
                double const colPos = afwImage::indexToPosition(cnvX);

                KernelPixelT kSum = kernel.computeImage(kernelImage, false, colPos, rowPos);
                KernelXYLocator kernelLoc = kernelImage.xy_at(0,0);
                *cnvXIter = afwMath::convolveAtAPoint<OutImageT, InImageT>(inImLoc, kernelLoc, kWidth, kHeight);
                if (doNormalize) {
                    *cnvXIter = *cnvXIter/kSum;
                }
            }
        }
    } else {
        lsst::pex::logging::TTrace<3>("lsst.afw.kernel.convolve", "kernel is spatially invariant");
        (void)kernel.computeImage(kernelImage, doNormalize);
        
        for (int inStartY = 0, cnvY = cnvStartY; inStartY < cnvHeight; ++inStartY, ++cnvY) {
            for (OutXIterator cnvXIter=convolvedImage.x_at(cnvStartX, cnvY),
                cnvXEnd = convolvedImage.row_end(cnvY); cnvXIter != cnvXEnd; ++cnvXIter) {
                *cnvXIter = 0;
            }
            for (int kernelY = 0, inY = inStartY; kernelY < kHeight; ++inY, ++kernelY) {
                KernelXIterator kernelXIter = kernelImage.x_at(0, kernelY);
                InXYIterator inXIter = inImage.x_at(0, inY);
                OutXIterator cnvXIter = convolvedImage.x_at(cnvStartX, cnvY);
                for (int x = 0; x < cnvWidth; ++x, ++cnvXIter, ++inXIter) {
                    // template parameters must be explicitly specified, at least for g++ 4.0.1
                    convolveOneKernelRow<OutImageT, InImageT>(cnvXIter, inXIter, kernelXIter, kWidth);
                }
            }
        }
    }
}

/************************************************************************************************************/
/**
 * @brief A version of basicConvolve that should be used when convolving delta function kernels
 */
template <typename OutImageT, typename InImageT>
void afwMath::basicConvolve(
    OutImageT& convolvedImage,      ///< convolved image
    InImageT const& inImage,        ///< image to convolve
    afwMath::DeltaFunctionKernel const &kernel,    ///< convolution kernel
    bool doNormalize    ///< if True, normalize the kernel, else use "as is"
) {
    assert (!kernel.isSpatiallyVarying());

    if (convolvedImage.getDimensions() != inImage.getDimensions()) {
        throw LSST_EXCEPT(pexExcept::InvalidParameterException, "convolvedImage not the same size as inImage");
    }
    if (convolvedImage.getDimensions() < kernel.getDimensions()) {
        throw LSST_EXCEPT(pexExcept::InvalidParameterException, "inImage smaller than kernel in columns and/or rows");
    }
    
    int const mImageWidth = inImage.getWidth(); // size of input region
    int const mImageHeight = inImage.getHeight();
    int const cnvWidth = mImageWidth + 1 - kernel.getWidth();
    int const cnvHeight = mImageHeight + 1 - kernel.getHeight();
    int const cnvStartX = kernel.getCtrX();
    int const cnvStartY = kernel.getCtrY();
    int const inStartX = kernel.getPixel().first;
    int const inStartY = kernel.getPixel().second;

    lsst::pex::logging::TTrace<3>("lsst.afw.kernel.convolve", "kernel is a spatially invariant delta function basis");

    for (int i = 0; i < cnvHeight; ++i) {
        typename InImageT::x_iterator inPtr = inImage.x_at(inStartX, i +  inStartY);
        for (typename OutImageT::x_iterator cnvPtr = convolvedImage.x_at(cnvStartX, i + cnvStartY),
                 cnvEnd = cnvPtr + cnvWidth; cnvPtr != cnvEnd; ++cnvPtr, ++inPtr){
            *cnvPtr = *inPtr;
        }
    }
}

/**
 * @brief A version of basicConvolve that should be used when convolving separable kernels
 */
template <typename OutImageT, typename InImageT>
void afwMath::basicConvolve(
    OutImageT& convolvedImage,      ///< convolved image
    InImageT const& inImage,        ///< image to convolve
    afwMath::SeparableKernel const &kernel, ///< convolution kernel
    bool doNormalize                ///< if True, normalize the kernel, else use "as is"
) {
    typedef typename afwMath::Kernel::PixelT KernelPixelT;

    typedef typename InImageT::const_xy_locator InXYLocator;
    typedef typename OutImageT::x_iterator OutXIterator;

    if (convolvedImage.getDimensions() != inImage.getDimensions()) {
        throw LSST_EXCEPT(pexExcept::InvalidParameterException, "convolvedImage not the same size as inImage");
    }
    if (inImage.getDimensions() < kernel.getDimensions()) {
        throw LSST_EXCEPT(pexExcept::InvalidParameterException, "inImage smaller than kernel in columns and/or rows");
    }
    
    int const imWidth = inImage.getWidth();
    int const imHeight = inImage.getHeight();
    int const cnvWidth = static_cast<int>(imWidth) + 1 - static_cast<int>(kernel.getWidth());
    int const cnvHeight = static_cast<int>(imHeight) + 1 - static_cast<int>(kernel.getHeight());
    int const cnvStartX = static_cast<int>(kernel.getCtrX());
    int const cnvStartY = static_cast<int>(kernel.getCtrY());
    int const cnvEndX = cnvStartX + static_cast<int>(cnvWidth); // end index + 1
    int const cnvEndY = cnvStartY + static_cast<int>(cnvHeight); // end index + 1

    std::vector<afwMath::Kernel::PixelT> kXVec(kernel.getWidth());
    std::vector<afwMath::Kernel::PixelT> kYVec(kernel.getHeight());
    
    if (kernel.isSpatiallyVarying()) {
        lsst::pex::logging::TTrace<3>("lsst.afw.kernel.convolve", "kernel is a spatially varying separable kernel");

        for (int cnvY = cnvStartY; cnvY != cnvEndY; ++cnvY) {
            double const rowPos = afwImage::indexToPosition(cnvY);
            
            InXYLocator  inImLoc =  inImage.xy_at(0, cnvY - cnvStartY);
            OutXIterator cnvXIter = convolvedImage.row_begin(cnvY) + cnvStartX;
            for (int cnvX = cnvStartX; cnvX != cnvEndX; ++cnvX, ++inImLoc.x(), ++cnvXIter) {
                double const colPos = afwImage::indexToPosition(cnvX);

                KernelPixelT kSum = kernel.computeVectors(kXVec, kYVec, doNormalize, colPos, rowPos);

                *cnvXIter = afwMath::convolveAtAPoint<OutImageT, InImageT>(inImLoc, kXVec, kYVec);
                if (doNormalize) {
                    *cnvXIter = *cnvXIter/kSum;
                }
            }
        }
    } else {
        // kernel is spatially invariant
        lsst::pex::logging::TTrace<3>("lsst.afw.kernel.convolve", "kernel is a spatially invariant separable kernel");

        (void)kernel.computeVectors(kXVec, kYVec, doNormalize);

        for (int cnvY = cnvStartY; cnvY != cnvEndY; ++cnvY) {
            InXYLocator inImLoc =  inImage.xy_at(0, cnvY - cnvStartY);
            for (OutXIterator cnvXIter = convolvedImage.row_begin(cnvY) + cnvStartX,
                     cnvImEnd = cnvXIter + (cnvEndX - cnvStartX); cnvXIter != cnvImEnd; ++inImLoc.x(), ++cnvXIter) {
                *cnvXIter = afwMath::convolveAtAPoint<OutImageT, InImageT>(inImLoc, kXVec, kYVec);
            }
        }
    }
}

/**
 * @brief Convolve an Image with a Kernel, setting pixels of an existing image
 *
 * convolvedImage must be the same size as inImage.
 * convolvedImage has a border in which the output pixels are just a copy of the input pixels.
 * This border has size:
 * * kernel.getCtrX/Y() along the left/bottom edge
 * * kernel.getWidth/Height() - 1 - kernel.getCtrY/Y() along the right/top edge
 *
 * @throw lsst::pex::exceptions::InvalidParameterException if convolvedImage is not the same size as inImage.
 * @throw lsst::pex::exceptions::InvalidParameterException if inImage is smaller (in colums or rows) than kernel.
 *
 * @ingroup afw
 */
template <typename OutImageT, typename InImageT, typename KernelT>
void afwMath::convolve(
    OutImageT& convolvedImage,          ///< convolved image
    InImageT const& inImage,            ///< image to convolve
    KernelT const& kernel,              ///< convolution kernel
    bool doNormalize,                   ///< if True, normalize the kernel, else use "as is"
    int edgeBit     ///< mask bit to indicate pixel includes edge-extended data;
                    ///< if negative (default) then no bit is set; only relevant for MaskedImages
) {
    afwMath::basicConvolve(convolvedImage, inImage, kernel, doNormalize);
    copyBorder(convolvedImage, inImage, kernel, edgeBit);
}

/**
 * @brief Convolve an Image with a LinearCombinationKernel, setting pixels of an existing image.
 *
 * A variant of the convolve function that is faster for spatially varying LinearCombinationKernels.
 * For the sake of speed the kernel is NOT normalized. If you want normalization then call the standard
 * convolve function.
 *
 * The Algorithm:
 * Convolves the input Image by each basis kernel in turn, solves the spatial model
 * for that component and adds in the appropriate amount of the convolved image.
 *
 * @todo
 * * Perhaps use the LinearCombinationKernel's cached images of basis kernels instead of computing new images;
 *   but be careful: if the basis kernels are delta function kernels then this is the wrong thing to do!
 *   It may also be suboptimal for separable basis kernels.
 *
 * @throw lsst::pex::exceptions::InvalidParameterException if convolvedImage is not the same size as inImage.
 * @throw lsst::pex::exceptions::InvalidParameterException if inImage is smaller (in colums or rows) than kernel.
 *
 * @ingroup afw
 */
template <typename OutImageT, typename InImageT>
void afwMath::convolveLinear(
    OutImageT& convolvedImage,      ///< convolved image
    InImageT const& inImage,        ///< image to convolve
    afwMath::LinearCombinationKernel const& kernel, ///< convolution kernel
    int edgeBit     ///< mask bit to indicate pixel includes edge-extended data;
                    ///< if negative (default) then no bit is set; only relevant for MaskedImages
                                    ) {
    if (!kernel.isSpatiallyVarying()) {
        return afwMath::convolve(convolvedImage, inImage, kernel, false, edgeBit);
    }

    if (convolvedImage.getDimensions() != inImage.getDimensions()) {
        throw LSST_EXCEPT(pexExcept::InvalidParameterException, "convolvedImage not the same size as inImage");
    }
    if (inImage.getDimensions() < kernel.getDimensions()) {
        throw LSST_EXCEPT(pexExcept::InvalidParameterException, "inImage smaller than kernel in columns and/or rows");
    }
    
    typedef typename InImageT::template ImageTypeFactory<double>::type BasisImage;
    typedef typename BasisImage::x_iterator BasisXIterator;
    typedef typename OutImageT::x_iterator OutXIterator;
    typedef afwMath::LinearCombinationKernel::KernelList KernelList;

    int const imWidth = inImage.getWidth();
    int const imHeight = inImage.getHeight();
    int const cnvWidth = imWidth + 1 - kernel.getWidth();
    int const cnvHeight = imHeight + 1 - kernel.getHeight();
    int const cnvStartX = kernel.getCtrX();
    int const cnvStartY = kernel.getCtrY();
    int const cnvEndX = cnvStartX + cnvWidth;  // end index + 1
    int const cnvEndY = cnvStartY + cnvHeight; // end index + 1
    // create a BasisImage to hold the source convolved with a basis kernel
    BasisImage basisImage(inImage.getDimensions());

    // initialize good area of output image to zero so we can add the convolved basis images into it
    // surely there is a single call that will do this? but in lieu of that...
    typename OutImageT::SinglePixel const nullPixel(0.0);
    for (int cnvY = cnvStartY; cnvY < cnvEndY; ++cnvY) {
        OutXIterator cnvXIter = convolvedImage.row_begin(cnvY) + cnvStartX;
        for (int cnvX = cnvStartX; cnvX != cnvEndX; ++cnvX, ++cnvXIter) {
            *cnvXIter = nullPixel;
        }
    }
    
    // iterate over basis kernels
    KernelList basisKernelList = kernel.getKernelList();
    int i = 0;
    for (typename KernelList::const_iterator basisKernelIter = basisKernelList.begin();
        basisKernelIter != basisKernelList.end(); ++basisKernelIter, ++i) {
        afwMath::basicConvolve(basisImage, inImage, **basisKernelIter, false);

        // iterate over matching pixels of all images to compute output image
        afwMath::Kernel::SpatialFunctionPtr spatialFunctionPtr = kernel.getSpatialFunction(i);
        std::vector<double> kernelCoeffList(kernel.getNKernelParameters()); // weights of basis images at this point
        for (int cnvY = cnvStartY; cnvY < cnvEndY; ++cnvY) {
            double const rowPos = afwImage::indexToPosition(cnvY);
        
            OutXIterator cnvXIter = convolvedImage.row_begin(cnvY) + cnvStartX;
            BasisXIterator basisXIter = basisImage.row_begin(cnvY) + cnvStartX;
            for (int cnvX = cnvStartX; cnvX != cnvEndX; ++cnvX, ++cnvXIter, ++basisXIter) {
                double const colPos = afwImage::indexToPosition(cnvX);
                double basisCoeff = (*spatialFunctionPtr)(colPos, rowPos);
                
                typename OutImageT::SinglePixel cnvPixel(*cnvXIter);
                cnvPixel = afwImage::pixel::plus(cnvPixel, (*basisXIter) * basisCoeff, 1.0);
                *cnvXIter = cnvPixel;
                // note: cnvPixel avoids compiler complaints; the following does not build:
                // *cnvXIter = afwImage::pixel::plus(*cnvXIter, (*basisXIter) * basisCoeff, 1.0);
            }
        }
    }
    copyBorder(convolvedImage, inImage, kernel, edgeBit);
}

/************************************************************************************************************/
/*
 *  Explicit instantiation of all convolve functions.
 *
 * This code needs to be compiled with full optimisation, and there's no need why
 * it should be instantiated in the swig wrappers.
 */
namespace lsst { namespace afw { namespace math {

#define IMAGE(PIXTYPE) afwImage::Image<PIXTYPE>
#define MASKEDIMAGE(PIXTYPE) afwImage::MaskedImage<PIXTYPE, afwImage::MaskPixel, afwImage::VariancePixel>
//
// Next a macro to generate needed instantiations for IMAGE (e.g. MASKEDIMAGE) and the specified pixel types
//
// Note that IMAGE is a macro, not a class name
//
/* NL's a newline for debugging -- don't define it and say
 g++ -C -E -I$(eups list -s -d boost)/include Convolve.cc | perl -pe 's| *NL *|\n|g'
*/
#define NL /* */
#define convolutionFuncsByType(IMAGE, PIXTYPE1, PIXTYPE2) \
    template void convolve(IMAGE(PIXTYPE1)&, IMAGE(PIXTYPE2) const&, AnalyticKernel const&, bool, int); NL \
    template void convolve(IMAGE(PIXTYPE1)&, IMAGE(PIXTYPE2) const&, DeltaFunctionKernel const&, bool, int); NL \
    template void convolve(IMAGE(PIXTYPE1)&, IMAGE(PIXTYPE2) const&, FixedKernel const&, bool, int); NL \
    template void convolve(IMAGE(PIXTYPE1)&, IMAGE(PIXTYPE2) const&, LinearCombinationKernel const&, bool, int); NL \
    template void convolveLinear(IMAGE(PIXTYPE1)&, IMAGE(PIXTYPE2) const&, LinearCombinationKernel const&, int); NL \
    template void convolve(IMAGE(PIXTYPE1)&, IMAGE(PIXTYPE2) const&, SeparableKernel const&, bool, int); NL \
    template void convolve(IMAGE(PIXTYPE1)&, IMAGE(PIXTYPE2) const&, Kernel const&, bool, int);

//
// Now a macro to specify Image and MaskedImage
//
#define convolutionFuncs(PIXTYPE1, PIXTYPE2) \
    convolutionFuncsByType(IMAGE,       PIXTYPE1, PIXTYPE2) \
    convolutionFuncsByType(MASKEDIMAGE, PIXTYPE1, PIXTYPE2)

convolutionFuncs(int, int)
convolutionFuncs(double, double)
convolutionFuncs(double, float)
convolutionFuncs(float, float)
convolutionFuncs(boost::uint16_t, boost::uint16_t)

}}}
