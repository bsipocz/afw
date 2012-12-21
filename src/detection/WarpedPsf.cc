// #include <boost/make_shared.hpp>
#include "lsst/afw/detection/WarpedPsf.h"
#include "lsst/afw/math/warpExposure.h"
#include "lsst/afw/math/detail/SrcPosFunctor.h"

namespace afwGeom = lsst::afw::geom;
namespace afwMath = lsst::afw::math;
namespace afwImage = lsst::afw::image;

typedef afwGeom::Point2D Point2D;

namespace lsst {
namespace afw {
namespace detection {


// -------------------------------------------------------------------------------------------------


static inline double min4(double a, double b, double c, double d)
{
    return std::min(std::min(a,b), std::min(c,d));
}

static inline double max4(double a, double b, double c, double d)
{
    return std::max(std::max(a,b), std::max(c,d));
}

//
// We preserve the convention of warpCenteredImage() that the affine transform is inverted,
// so that the output and input images are related by:
//   out[p] = in[A^{-1}p]
//
// The input image is assumed zero-padded.
//
static inline Psf::Image::Ptr warpAffine(Psf::Image const &im, afwGeom::AffineTransform const &t)
{
    // min/max coordinate values in input image
    int in_xlo = im.getX0();
    int in_xhi = im.getX0() + im.getWidth() - 1;
    int in_ylo = im.getY0();
    int in_yhi = im.getY0() + im.getHeight() - 1;

    // corners of output image
    Point2D c00 = t(Point2D(in_xlo,in_ylo));
    Point2D c01 = t(Point2D(in_xlo,in_yhi));
    Point2D c10 = t(Point2D(in_xhi,in_ylo));
    Point2D c11 = t(Point2D(in_xhi,in_yhi));

    //
    // bounding box for output image (currently we use the smallest box which
    // contains the corners, but it might be reasonable to enlarge a little to
    // avoid edge artifacts from the interpolation)
    //
    int out_xlo = floor(min4(c00.getX(),c01.getX(),c10.getX(),c11.getX()));
    int out_ylo = floor(min4(c00.getY(),c01.getY(),c10.getY(),c11.getY()));
    int out_xhi = ceil(max4(c00.getX(),c01.getX(),c10.getX(),c11.getX()));
    int out_yhi = ceil(max4(c00.getY(),c01.getY(),c10.getY(),c11.getY()));

    // make output image
    Psf::Image::Ptr ret = boost::make_shared<Psf::Image>(out_xhi-out_xlo+1, out_yhi-out_ylo+1);
    ret->setXY0(afwGeom::Point2I(out_xlo,out_ylo));

    //
    // warp it!
    // FIXME what type of interpolation is best here?  Currently using lanczos5, somewhat arbitrarily...
    //
    afwMath::WarpingControl wc("lanczos5");
    afwMath::warpImage(*ret, im, t, wc);

    return ret;
}


// -------------------------------------------------------------------------------------------------


WarpedPsf::WarpedPsf(Psf::Ptr undistorted_psf, XYTransform::Ptr distortion)
{
    _undistorted_psf = undistorted_psf;
    _distortion = distortion;
}

Psf::Ptr WarpedPsf::clone() const
{
    return boost::make_shared<WarpedPsf>(_undistorted_psf->clone(), _distortion->clone());
}

Psf::Image::Ptr WarpedPsf::doComputeImage(Color const& color, Point2D const& ccdXY, Extent2I const& size, bool normalizePeak, bool distort) const
{
    Point2I ctr;
    PTR(Image) im = this->_make_warped_kernel_image(ccdXY, color, ctr);
    
    int width = (size.getX() > 0) ? size.getX() : im->getWidth();
    int height = (size.getY() > 0) ? size.getY() : im->getHeight();

    if ((width != im->getWidth()) || (height != im->getHeight())) {
	PTR(Image) im2 = boost::make_shared<Image> (width, height);
	ctr = resizeKernelImage(*im2, *im, ctr);
	im = im2;
    }

    if (normalizePeak) {
	double centralPixelValue = (*im)(ctr.getX(), ctr.getY());
	*im /= centralPixelValue;
    }

    return recenterKernelImage(im, ctr, ccdXY);
}

afwMath::Kernel::Ptr WarpedPsf::_doGetLocalKernel(Point2D const &p, Color const &c) const
{
    Point2I ctr;
    PTR(Image) im = this->_make_warped_kernel_image(p, c, ctr);
    PTR(afwMath::Kernel) ret = boost::make_shared<afwMath::FixedKernel>(*im);
    ret->setCtr(ctr);
    return ret;
}

Psf::Image::Ptr WarpedPsf::_make_warped_kernel_image(Point2D const &p, Color const &c, Point2I &ctr) const
{
    afwGeom::AffineTransform t = _distortion->linearizeForwardTransform(p);
    Point2D tp = t(p);

    Kernel::Ptr k = _undistorted_psf->getLocalKernel(tp, color);
    PTR(Image) im = boost::make_shared<Image>(k->getWidth(), k->getHeight());

    // XXX normalization
    k->computeImage(*im, true, tp.getX(), tp.getY());
    im.setXY0(Point2I(-k->getCtrX(), -k->getCtrY()));

    Psf::Image::Ptr ret = warpCenteredImage(*im, t.getLinear(), Point2D(0,0));
    ctr = -ret->getXY0();
}


}}}

