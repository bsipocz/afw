// -*- lsst-c++ -*-
/*
 * LSST Data Management System
 * Copyright 2008-2014 LSST Corporation.
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

#ifndef LSST_AFW_DETECTION_FOOTPRINTMERGE_H
#define LSST_AFW_DETECTION_FOOTPRINTMERGE_H

#include <vector>
#include <map>

#include "lsst/afw/table/Source.h"

namespace lsst { namespace afw { namespace detection {

/**
 *  FootprintMerge is a private helper class for FootprintMergeList; it's only declared here (it's defined
 *  in the .cc file) so FootprintMergeList can hold a vector without an extra PImpl layer, and so Footprint
 *  can friend it.
 */
class FootprintMerge;

/**
 *  @brief List of Merged Footprints.
 *
 *  Stores a vector of FootprintMerges and SourceRecords that contain the union of different footprints and
 *  which filters it was detected in.  Individual Footprints from a SourceCatalog can be added to
 *  the vector (note that any SourceRecords with parent!=0 will be skipped).  If a Footprint overlaps an
 *  existing FootprintMerge, the Footprint will be added to it.  If not, then a new FootprintMerge will be
 *  created and added to the vector.
 *
 *  The search algorithm uses a brute force approach over the current list.  This should be fine if we 
 *  are operating on smallish number of objects, such as at the tract level.
 *
 */
class FootprintMergeList {
public:

    FootprintMergeList(afw::table::Schema & sourceSchema,
                       std::vector<std::string> const &filterList);

    /**
     *  @brief Add objects from a SourceCatalog in the specified filter
     *
     *  Iterate over all objects that have not been deblendend and search for an overlapping
     *  FootprintMerge in _mergeList.  If it overlaps, then it will be added to it,
     *  otherwise it will create a new one.  If minNewPeakDist < 0, then new peaks will
     *  not be added to existing footprints.  If minNewPeakDist >= 0, then new peaks will be added
     *  that are farther away than minNewPeakDist to the nearest existing peak.
     *
     *  The SourceTable is used to create new SourceRecords that store the filter information.
     */
    void addCatalog(PTR(afw::table::SourceTable) sourceTable, afw::table::SourceCatalog const &inputCat,
                    std::string const & filter, float minNewPeakDist=-1., bool doMerge=true);

    /**
     *  @brief Clear entries in the current vector
     */
    void clearCatalog() { _mergeList.clear(); }

    /**
     *  @brief Get SourceCatalog with entries that contain the final Footprint and SourceRecord for each entry
     *
     *  The resulting Footprints will be normalized, meaning that there peaks are sorted, and
     *  areas are calculated.
     */
    void getFinalSources(afw::table::SourceCatalog &outputCat, bool doNorm=true);

private:

    typedef afw::table::Key<afw::table::Flag> FlagKey;
    typedef std::vector<PTR(FootprintMerge)> FootprintMergeVec;
    typedef std::map<std::string,FlagKey> FilterMap;

    friend class FootprintMerge;

    FootprintMergeVec _mergeList;
    FilterMap _filterMap;
};

}}} // namespace lsst::afw::detection

#endif // !LSST_AFW_DETECTION_FOOTPRINTMERGE_H