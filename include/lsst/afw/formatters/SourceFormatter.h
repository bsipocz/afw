// -*- lsst-c++ -*-

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
 
//
//##====----------------                                ----------------====##/
//
//! \file
//! \brief  Formatter subclasses for Source
//!         and Persistable containers thereof.
//
//##====----------------                                ----------------====##/

#ifndef LSST_AFW_FORMATTERS_SOURCE_FORMATTERS_H
#define LSST_AFW_FORMATTERS_SOURCE_FORMATTERS_H

#include <string>
#include <vector>

#include "lsst/daf/base.h"
#include "lsst/daf/persistence.h"
#include "lsst/afw/detection/Source.h"

namespace lsst {
namespace afw {
namespace formatters {

/*!
    Formatter that supports persistence and retrieval with

    - lsst::daf::persistence::DbStorage
    - lsst::daf::persistence::DbTsvStorage
    - lsst::daf::persistence::BoostStorage

    for PersistableSourceVector instances.
 */
class SourceVectorFormatter : public lsst::daf::persistence::Formatter {
public:

    virtual ~SourceVectorFormatter();

    virtual void write(
        lsst::daf::base::Persistable const *,
        lsst::daf::persistence::Storage::Ptr,
        lsst::daf::base::PropertySet::Ptr
    );
    virtual lsst::daf::base::Persistable* read(
        lsst::daf::persistence::Storage::Ptr,
        lsst::daf::base::PropertySet::Ptr
    );
    virtual void update(
        lsst::daf::base::Persistable*,
        lsst::daf::persistence::Storage::Ptr,
        lsst::daf::base::PropertySet::Ptr
    );


    template <class Archive>
    static void delegateSerialize(
        Archive &,
        unsigned int const,
        lsst::daf::base::Persistable *
    );


private:

    // ordered list of columns in Source table of the DC3b schema
    enum Columns {
        SOURCE_ID = 0,
        AMP_EXPOSURE_ID,
        FILTER_ID,
        OBJECT_ID,
        MOVING_OBJECT_ID,
        PROC_HISTORY_ID,
        RA,
        RA_ERR_FOR_DETECTION,
        RA_ERR_FOR_WCS,
        DECL,
        DEC_ERR_FOR_DETECTION,
        DEC_ERR_FOR_WCS,
        X_FLUX,
        X_FLUX_ERR,
        Y_FLUX,
        Y_FLUX_ERR,
        RA_FLUX,
        RA_FLUX_ERR,
        DEC_FLUX,
        DEC_FLUX_ERR,
        X_PEAK,
        Y_PEAK,
        RA_PEAK,
        DEC_PEAK,
        X_ASTROM,
        X_ASTROM_ERR,
        Y_ASTROM,
        Y_ASTROM_ERR,
        RA_ASTROM,
        RA_ASTROM_ERR,
        DEC_ASTROM,
        DEC_ASTROM_ERR,
        RA_OBJECT,
        DEC_OBJECT,
        TAI_MID_POINT,
        TAI_RANGE,
        PSF_FLUX,
        PSF_FLUX_ERR,
        AP_FLUX,
        AP_FLUX_ERR,
        MODEL_FLUX,
        MODEL_FLUX_ERR,
        PETRO_FLUX,
        PETRO_FLUX_ERR,
        INST_FLUX,
        INST_FLUX_ERR,
        NON_GRAY_CORR_FLUX,
        NON_GRAY_CORR_FLUX_ERR,
        ATM_CORR_FLUX,
        ATM_CORR_FLUX_ERR,
        AP_DIA,
        IXX,
        IXX_ERR,
        IYY,
        IYY_ERR,
        IXY,
        IXY_ERR,
        SNR,
        CHI2,
        SKY,
        SKY_ERR,
        FLAG_FOR_ASSOCIATION,
        FLAG_FOR_DETECTION,
        FLAG_FOR_WCS
    };


    lsst::pex::policy::Policy::Ptr _policy;

    explicit SourceVectorFormatter(lsst::pex::policy::Policy::Ptr const & policy);

    static lsst::daf::persistence::Formatter::Ptr createInstance(
        lsst::pex::policy::Policy::Ptr
    );
    static lsst::daf::persistence::FormatterRegistration registration;

    template <typename T>
    static void insertRow(
        T &,
        lsst::afw::detection::Source const &
    );
};


}}} // namespace lsst::afw::formatters

#endif // LSST_AFW_FORMATTERS_SOURCE_FORMATTERS_H

