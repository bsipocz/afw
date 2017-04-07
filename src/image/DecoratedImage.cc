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
 * An Image with associated metadata
 */
#include <cstdint>
#include <iostream>
#include "boost/format.hpp"

#include "lsst/pex/exceptions.h"
#include "lsst/afw/image/Image.h"

namespace image = lsst::afw::image;
namespace geom = lsst::afw::geom;

template<typename PixelT>
void image::DecoratedImage<PixelT>::init() {
    // safer to initialize a smart pointer as a named variable
    PTR(daf::base::PropertySet) metadata(new daf::base::PropertyList);
    setMetadata(metadata);
    _gain = 0;
}

template<typename PixelT>
image::DecoratedImage<PixelT>::DecoratedImage(
    geom::Extent2I const & dimensions
) :
    lsst::daf::base::Citizen(typeid(this)),
    _image(new Image<PixelT>(dimensions))
{
    init();
}
template<typename PixelT>
image::DecoratedImage<PixelT>::DecoratedImage(
    geom::Box2I const & bbox
) :
    lsst::daf::base::Citizen(typeid(this)),
    _image(new Image<PixelT>(bbox))
{
    init();
}
template<typename PixelT>
image::DecoratedImage<PixelT>::DecoratedImage(
    PTR(Image<PixelT>) rhs
) :
    lsst::daf::base::Citizen(typeid(this)),
    _image(rhs)
{
    init();
}
template<typename PixelT>
image::DecoratedImage<PixelT>::DecoratedImage(
    const DecoratedImage& src,
    const bool deep
) :
    lsst::daf::base::Citizen(typeid(this)),
    _image(new Image<PixelT>(*src._image, deep)), _gain(src._gain)
{
    setMetadata(src.getMetadata());
}
template<typename PixelT>
image::DecoratedImage<PixelT>& image::DecoratedImage<PixelT>::operator=(const DecoratedImage& src) {
    DecoratedImage tmp(src);
    swap(tmp);                          // See Meyers, Effective C++, Item 11

    return *this;
}

template<typename PixelT>
void image::DecoratedImage<PixelT>::swap(DecoratedImage &rhs) {
    using std::swap;                    // See Meyers, Effective C++, Item 25

    swap(_image, rhs._image);           // just swapping the pointers
    swap(_gain, rhs._gain);
}

template<typename PixelT>
void image::swap(DecoratedImage<PixelT>& a, DecoratedImage<PixelT>& b) {
    a.swap(b);
}

//
// FITS code
//
#include <boost/mpl/vector.hpp>

#include "lsst/pex/exceptions.h"
#include "lsst/afw/image/Image.h"

#include "boost/gil/gil_all.hpp"
#include "lsst/afw/image/fits/fits_io.h"
#include "lsst/afw/image/fits/fits_io_mpl.h"
template<typename PixelT>
image::DecoratedImage<PixelT>::DecoratedImage(const std::string& fileName,
                                              const int hdu,
                                              geom::Box2I const& bbox,
                                              ImageOrigin const origin
                                             ) :
    lsst::daf::base::Citizen(typeid(this))
{
    init();
    _image = typename Image<PixelT>::Ptr(new Image<PixelT>(fileName, hdu, getMetadata(), bbox, origin));
}

template<typename PixelT>
void image::DecoratedImage<PixelT>::writeFits(
    std::string const& fileName,
    CONST_PTR(daf::base::PropertySet) metadata_i,
    std::string const& mode
) const {
    lsst::daf::base::PropertySet::Ptr metadata;

    if (metadata_i) {
        metadata = getMetadata()->deepCopy();
        metadata->combine(metadata_i);
    } else {
        metadata = getMetadata();
    }

    getImage()->writeFits(fileName, metadata, mode);
}

//
// Explicit instantiations
//
template class image::DecoratedImage<std::uint16_t>;
template class image::DecoratedImage<int>;
template class image::DecoratedImage<float>;
template class image::DecoratedImage<double>;
template class image::DecoratedImage<std::uint64_t>;

