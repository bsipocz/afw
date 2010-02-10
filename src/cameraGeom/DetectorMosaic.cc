/**
 * \file
 */
#include <algorithm>
#include "lsst/afw/cameraGeom/DetectorMosaic.h"

namespace pexExcept = lsst::pex::exceptions;
namespace afwGeom = lsst::afw::geom;
namespace afwImage = lsst::afw::image;
namespace cameraGeom = lsst::afw::cameraGeom;

/// Set the DetectorMosaic's center
void cameraGeom::DetectorMosaic::setCenter(afwGeom::Point2D const& center) {
    cameraGeom::Detector::setCenter(center);
    //
    // Update the centers for all children too
    //
    for (cameraGeom::DetectorMosaic::const_iterator ptr = begin(), end = this->end(); ptr != end; ++ptr) {
        (*ptr)->setCenter(afwGeom::Extent2D((*ptr)->getCenter()) + center);
    }
}

/************************************************************************************************************/
/**
 * Return a DetectorMosaic's size in mm
 */
afwGeom::Extent2D cameraGeom::DetectorMosaic::getSize() const {
    afwGeom::Extent2D size(0.0);        // the desired size
    // This code can probably use afwGeom's bounding boxes when they are available
    afwGeom::Point2D LLC, URC;          // corners of DetectorMosaic

    for (cameraGeom::DetectorMosaic::const_iterator ptr = begin(), end = this->end(); ptr != end; ++ptr) {
        cameraGeom::Detector::Ptr det = *ptr;
        afwGeom::Extent2D detectorSize = det->getSize();

        double const yaw = det->getOrientation().getYaw();
        if (yaw != 0.0) {
            throw LSST_EXCEPT(pexExcept::RangeErrorException,
                              (boost::format("(yaw == %f) != 0 is not supported for Detector %||") %
                               yaw % det->getId()).str());
        }

        if (ptr == begin()) {           // first detector
            LLC = det->getCenter() - detectorSize/2;
            URC = det->getCenter() + detectorSize/2;
        } else {
            afwGeom::Point2D llc = det->getCenter() - detectorSize/2; // corners of this Detector
            afwGeom::Point2D urc = det->getCenter() + detectorSize/2;

            if (llc[0] < LLC[0]) {      // X
                LLC[0] = llc[0];
            }
            if (llc[1] < LLC[1]) {      // Y
                LLC[1] = llc[1];
            }

            if (urc[0] > URC[0]) {      // X
                URC[0] = urc[0];
            }
            if (urc[1] > URC[1]) {      // Y
                URC[1] = urc[1];
            }
        }
    }

    return URC - LLC;
}

/**
 * Add an Detector to the set known to be part of this DetectorMosaic
 *
 *  The \c index is the 0-indexed position of the Detector in the DetectorMosaic; e.g. (0, 2)
 * for the top left Detector in a 3x3 mosaic
 */
void cameraGeom::DetectorMosaic::addDetector(
        afwGeom::Point2I const& index, ///< index of this Detector in DetectorMosaic thought of as a grid
        afwGeom::Point2D const& center, ///< center of this Detector wrt center of DetectorMosaic
        cameraGeom::Orientation const& orient, ///< orientation of this Detector
        cameraGeom::Detector::Ptr det   ///< The detector to add to the DetectorMosaic's manifest.
                                            )
{
    bool const isTrimmed = true;        // We always work in trimmed coordinates at the DetectorMosaic level
    int const iX = index[0];
    int const iY = index[1];
    
    if (iX < 0 || iX >= _nDetector.first) {
        throw LSST_EXCEPT(pexExcept::RangeErrorException,
                          (boost::format("Col index %d is not in range 0..%d for Detector %||") %
                           iX % _nDetector.first % det->getId()).str());
    }
    if (iY < 0 || iY >= _nDetector.second) {
        throw LSST_EXCEPT(pexExcept::RangeErrorException,
                          (boost::format("Row index %d is not in range 0..%d for Detector %||") %
                           iY % _nDetector.second % det->getId()).str());
    }
    //
    // Don't permit non-square Detectors to have relative rotations other than 0, 180
    //
    if (_detectors.size() > 0) {
        if ((orient.getNQuarter() - (*begin())->getOrientation().getNQuarter())%2 != 0 &&
            det->getAllPixels(true).getWidth() != det->getAllPixels(true).getHeight()) {
            throw LSST_EXCEPT(pexExcept::InvalidParameterException,
                              (boost::format("Rotation of detector %|| (nQuarter == %d) "
                                             "is incompatible with %|| (nQuarter == %d)") %
                               det->getId() % orient.getNQuarter() %
                               (*begin())->getId() % (*begin())->getOrientation().getNQuarter()).str());
        }
    }

    det->setOrientation(orient);
    //
    // If this is the first detector, set the center pixel.  We couldn't do this earlier as
    // we didn't know the detector size
    //
    if (_detectors.size() == 0) {
        setCenterPixel(afwGeom::makePointI(0.5*_nDetector.first*det->getAllPixels(isTrimmed).getWidth(),
                                               0.5*_nDetector.second*det->getAllPixels(isTrimmed).getHeight())
                      );
    }
    //
    // Correct Detector's coordinate system to be absolute within DetectorMosaic
    //
    afwImage::BBox detPixels = det->getAllPixels(isTrimmed);
    detPixels.shift(iX*detPixels.getWidth(), iY*detPixels.getHeight());

    getAllPixels().grow(detPixels.getLLC());
    getAllPixels().grow(detPixels.getURC());
    
    afwGeom::Point2I centerPixel =
        afwGeom::makePointI(iX*detPixels.getWidth() + detPixels.getWidth()/2,
                                 iY*detPixels.getHeight() + detPixels.getHeight()/2) -
        afwGeom::Extent2I(getCenterPixel());
    det->setCenter(center);
    det->setCenterPixel(centerPixel);

    // insert new Detector, keeping the Detectors sorted
    _detectors.insert(std::lower_bound(_detectors.begin(), _detectors.end(), det,
                                       cameraGeom::detail::sortPtr<Detector>()), det);
}

/************************************************************************************************************/

namespace {
    struct findById {
        findById(cameraGeom::Id id) : _id(id) {}
        bool operator()(cameraGeom::Detector::Ptr det) const {
            return _id == det->getId();
        }
    private:
        cameraGeom::Id _id;
    };

    struct findByPixel {
        findByPixel(afwGeom::Point2I point) :
            _point(afwImage::PointI(point[0], point[1])) {}
        
        bool operator()(cameraGeom::Detector::Ptr det) const {
            afwImage::PointI relPoint = _point;
            // Position wrt center of detector
            relPoint.shift(-det->getCenterPixel()[0],
                           -det->getCenterPixel()[1]);
            // Position wrt LLC of detector
            relPoint.shift(det->getAllPixels(true).getWidth()/2,
                           det->getAllPixels(true).getHeight()/2);

            return det->getAllPixels().contains(relPoint);
        }
    private:
        afwImage::PointI _point;
    };

    struct findByPos {
        findByPos(
                  afwGeom::Point2D point
                 )
            :
            _point(point)
        { }

        /*
         * Does point lie with square footprint of detector, once we allow for its rotation?
         */
        bool operator()(cameraGeom::Detector::Ptr det) const {
            afwGeom::Extent2D off = _point - det->getCenter();
            double const c = det->getOrientation().getCosYaw();
            double const s = det->getOrientation().getSinYaw();

            double const dx = off[0]*c - off[1]*s; // rotate into CCD frame
            double const xSize2 = det->getSize()[0]/2;  // xsize/2
            if (dx < -xSize2 || dx > xSize2) {
                return false;
            }

            double const dy = off[0]*s + off[1]*c; // rotate into CCD frame
            double const ySize2 = det->getSize()[1]/2;  // ysize/2
            if (dy < -ySize2 || dy > ySize2) {
                return false;
            }

            return true;
        }
    private:
        afwGeom::Point2D _point;
    };
}

/**
 * Find an Detector given an Id
 */
cameraGeom::Detector::Ptr cameraGeom::DetectorMosaic::findDetector(
        cameraGeom::Id const id         ///< The desired ID
                                                                        ) const {
    DetectorSet::const_iterator result = std::find_if(_detectors.begin(), _detectors.end(), findById(id));
    if (result == _detectors.end()) {
        throw LSST_EXCEPT(pexExcept::OutOfRangeException,
                          (boost::format("Unable to find Detector with serial %||") % id).str());
    }
    return *result;
}

/**
 * Find an Detector given a pixel position
 */
cameraGeom::Detector::Ptr cameraGeom::DetectorMosaic::findDetector(
        afwGeom::Point2I const& pixel,   ///< the desired pixel
        bool const fromCenter            ///< pixel is measured wrt the detector center, not LL corner
                                                                        ) const {
    if (!fromCenter) {
        return findDetector(pixel - afwGeom::makeExtentI(getAllPixels().getWidth()/2,
                                                              getAllPixels().getHeight()/2), true);
    }

    DetectorSet::const_iterator result =
        std::find_if(_detectors.begin(), _detectors.end(), findByPixel(pixel));
    if (result == _detectors.end()) {
        throw LSST_EXCEPT(pexExcept::OutOfRangeException,
                          (boost::format("Unable to find Detector containing pixel (%d, %d)") %
                           (pixel.getX() + getCenterPixel()[0]) %
                           (pixel.getY() + getCenterPixel()[1])).str());
    }
    return *result;
}

/**
 * Find an Detector given a physical position in mm
 */
cameraGeom::Detector::Ptr cameraGeom::DetectorMosaic::findDetector(
        afwGeom::Point2D const& pos     ///< the desired position; mm from the centre
                                                                        ) const {
    DetectorSet::const_iterator result = std::find_if(_detectors.begin(), _detectors.end(), findByPos(pos));
    if (result == _detectors.end()) {
        throw LSST_EXCEPT(pexExcept::OutOfRangeException,
                          (boost::format("Unable to find Detector containing pixel (%g, %g)") %
                           pos.getX() % pos.getY()).str());
    }
    return *result;
}

/**
 * Return the pixel position given an offset from the mosaic centre, in mm
 * \sa getIndexFromPosition
 */
afwGeom::Point2I cameraGeom::DetectorMosaic::getPixelFromPosition(
        afwGeom::Point2D const& pos     ///< Offset from mosaic centre, mm
                                                                 ) const
{
    cameraGeom::Detector::ConstPtr det = findDetector(pos);

    return afwGeom::Extent2I(getCenterPixel()) + det->getPixelFromPosition(pos);
}

/**
 * Return the pixel position given an offset from the detector centre, in mm
 * \sa getPixelFromPosition
 */
afwGeom::Point2I cameraGeom::DetectorMosaic::getIndexFromPosition(
        afwGeom::Point2D const& pos     ///< Offset from mosaic centre, mm
                                                                 ) const
{
    cameraGeom::Detector::ConstPtr det = findDetector(pos);

    return afwGeom::Point2I(det->getIndexFromPosition(pos - afwGeom::Extent2D(det->getCenter())));
}

/**
 * Return the offset from the mosaic centre, in mm, given a pixel position
 */
afwGeom::Point2D cameraGeom::DetectorMosaic::getPositionFromIndex(
        afwGeom::Point2I const& pix     ///< Pixel coordinates wrt centre of mosaic
                                                                 ) const
{
    cameraGeom::Detector::ConstPtr det = findDetector(pix, true);

    bool const isTrimmed = true;        ///< Detectors in Mosaics are always trimmed
    return det->getPositionFromIndex(pix - afwGeom::Extent2I(det->getCenterPixel()), isTrimmed);
}    
