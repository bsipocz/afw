#
# LSST Data Management System
# Copyright 2008-2016 LSST Corporation.
#
# This product includes software developed by the
# LSST Project (http://www.lsst.org/).
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the LSST License Statement and
# the GNU General Public License along with this program.  If not,
# see <http://www.lsstcorp.org/LegalNotices/>.
#

import os.path
import unittest

import numpy as np

import astropy.units as u

import lsst.utils.tests
import lsst.geom
import lsst.afw.image
import lsst.afw.image.testUtils
import lsst.afw.math
import lsst.daf.base
import lsst.pex.exceptions


def computeNanojanskyErr(instFluxErr, instFlux, calibrationErr, calibration, flux):
    """Return the error on the flux (nanojansky)."""
    return flux*np.hypot(instFluxErr/instFlux, calibrationErr/calibration)


def computeMagnitudeErr(instFluxErr, instFlux, calibrationErr, calibration, flux):
    """Return the error on the magnitude."""
    err = computeNanojanskyErr(instFluxErr, instFlux, calibrationErr, calibration, flux)
    return 2.5/np.log(10) * err / flux


def makeCalibratedMaskedImage(image, mask, variance, outImage, calibration, calibrationErr):
    """Return a MaskedImage using outImage, mask, and a computed variance image."""
    outErr = computeNanojanskyErr(np.sqrt(variance),
                                  image,
                                  calibrationErr,
                                  calibration,
                                  outImage).astype(np.float32)  # variance plane must be 32bit
    return lsst.afw.image.makeMaskedImageFromArrays(outImage,
                                                    mask,
                                                    outErr**2)


def makeCalibratedMaskedImageNoCalibrationError(image, mask, variance, outImage):
    """Return a MaskedImage using outImage, mask, and a computed variance image.

    Ignores the contributions from the uncertainty in the calibration.
    """
    outErr = computeNanojanskyErr(np.sqrt(variance),
                                  image,
                                  0,
                                  1,
                                  outImage).astype(np.float32)  # variance plane must be 32bit
    return lsst.afw.image.makeMaskedImageFromArrays(outImage,
                                                    mask,
                                                    outErr**2)


class PhotoCalibTestCase(lsst.utils.tests.TestCase):

    def setUp(self):
        np.random.seed(100)

        self.point0 = lsst.geom.Point2D(0, 0)
        self.pointXShift = lsst.geom.Point2D(-10, 0)
        self.pointYShift = lsst.geom.Point2D(0, -10)
        self.bbox = lsst.geom.Box2I(lsst.geom.Point2I(-100, -100), lsst.geom.Point2I(100, 100))

        # calibration and instFlux1 are selected to produce calibrated flux of 1.
        self.calibration = 1e-3
        self.calibrationErr = 1e-4
        self.instFlux1 = 1000.
        self.instFluxErr1 = 10.
        self.flux1 = 1.0
        self.mag1 = (self.flux1*u.nJy).to_value(u.ABmag)

        # useful reference points: 575.44 nJy ~= 24.5 mag, 3630.78 * 10^9 nJy ~= 0 mag
        self.flux2 = 575.44
        self.instFlux2 = self.instFlux1*self.flux2
        self.mag2 = (self.flux2*u.nJy).to_value(u.ABmag)

        self.schema = lsst.afw.table.SourceTable.makeMinimalSchema()
        self.instFluxKeyName = "SomeFlux"
        lsst.afw.table.Point2DKey.addFields(self.schema, "centroid", "centroid", "pixels")
        self.schema.addField(self.instFluxKeyName+"_instFlux", type="D", doc="post-ISR instrumental Flux")
        self.schema.addField(self.instFluxKeyName+"_instFluxErr", type="D",
                             doc="post-ISR instrumental flux stddev")
        self.schema.addField(self.instFluxKeyName+"_flux", type="D",
                             doc="calibrated flux (nJy)")
        self.schema.addField(self.instFluxKeyName+"_fluxErr", type="D",
                             doc="calibrated flux stddev (nJy)")
        self.schema.addField(self.instFluxKeyName+"_mag", type="D",
                             doc="calibrated magnitude")
        self.schema.addField(self.instFluxKeyName+"_magErr", type="D",
                             doc="calibrated magnitude stddev")
        self.table = lsst.afw.table.SourceTable.make(self.schema)
        self.table.defineCentroid('centroid')
        self.catalog = lsst.afw.table.SourceCatalog(self.table)
        record = self.catalog.addNew()
        record.set('id', 1)
        record.set('centroid_x', self.point0[0])
        record.set('centroid_y', self.point0[1])
        record.set(self.instFluxKeyName+'_instFlux', self.instFlux1)
        record.set(self.instFluxKeyName+'_instFluxErr', self.instFluxErr1)
        record = self.catalog.addNew()
        record.set('id', 2)
        record.set('centroid_x', self.pointYShift[0])
        record.set('centroid_y', self.pointYShift[1])
        record.set(self.instFluxKeyName+'_instFlux', self.instFlux2)
        record.set(self.instFluxKeyName+'_instFluxErr', self.instFluxErr1)

        self.constantCalibration = lsst.afw.math.ChebyshevBoundedField(self.bbox,
                                                                       np.array([[self.calibration]]))
        self.linearXCalibration = lsst.afw.math.ChebyshevBoundedField(self.bbox,
                                                                      np.array([[self.calibration,
                                                                               self.calibration]]))

    def tearDown(self):
        del self.schema
        del self.table
        del self.catalog

    def _testPhotoCalibCenter(self, photoCalib, calibrationErr):
        """
        Test conversions of instFlux for the mean and (0,0) value of a photoCalib.
        Assumes those are the same, e.g. that the non-constant terms are all
        odd, and that the mean of the calib is self.calibration.

        calibrationErr is provided as an option to allow testing of photoCalibs
        that have no error specified, and those that do.
        """
        # test that the constructor set the calibrationMean and err correctly
        self.assertEqual(self.calibration, photoCalib.getCalibrationMean())
        self.assertEqual(photoCalib.instFluxToMagnitude(photoCalib.getInstFluxAtZeroMagnitude()), 0)
        self.assertEqual(calibrationErr, photoCalib.getCalibrationErr())

        # test with a "trivial" flux
        self.assertEqual(self.flux1, photoCalib.instFluxToNanojansky(self.instFlux1))
        self.assertEqual(self.mag1, photoCalib.instFluxToMagnitude(self.instFlux1))

        # a less trivial flux
        self.assertFloatsAlmostEqual(self.flux2, photoCalib.instFluxToNanojansky(self.instFlux2))
        self.assertFloatsAlmostEqual(self.mag2, photoCalib.instFluxToMagnitude(self.instFlux2))
        # test that (0,0) gives the same result as above
        self.assertFloatsAlmostEqual(self.flux2, photoCalib.instFluxToNanojansky(self.instFlux2, self.point0))
        self.assertFloatsAlmostEqual(self.mag2, photoCalib.instFluxToMagnitude(self.instFlux2, self.point0))

        # test that we get a correct nJy err for the base instFlux
        errFlux1 = computeNanojanskyErr(self.instFluxErr1,
                                        self.instFlux1,
                                        calibrationErr,
                                        self.calibration,
                                        self.flux1)
        result = photoCalib.instFluxToNanojansky(self.instFlux1, self.instFluxErr1)
        self.assertEqual(1, result.value)
        self.assertFloatsAlmostEqual(errFlux1, result.error)
        result = photoCalib.instFluxToNanojansky(self.instFlux1, self.instFluxErr1, self.point0)
        self.assertFloatsAlmostEqual(self.flux1, result.value)
        self.assertFloatsAlmostEqual(errFlux1, result.error)

        # test that we get a correct magnitude err for the base instFlux
        errMag1 = computeMagnitudeErr(self.instFluxErr1,
                                      self.instFlux1,
                                      calibrationErr,
                                      self.calibration,
                                      self.flux1)
        result = photoCalib.instFluxToMagnitude(self.instFlux1, self.instFluxErr1)
        self.assertEqual(self.mag1, result.value)
        self.assertFloatsAlmostEqual(errMag1, result.error)
        # and the same given an explicit point at the center
        result = photoCalib.instFluxToMagnitude(self.instFlux1, self.instFluxErr1, self.point0)
        self.assertFloatsAlmostEqual(self.mag1, result.value)
        self.assertFloatsAlmostEqual(errMag1, result.error)

        # test that we get a correct nJy err for flux2
        errFlux2 = computeNanojanskyErr(self.instFluxErr1,
                                        self.instFlux2,
                                        calibrationErr,
                                        self.calibration,
                                        self.flux2)
        result = photoCalib.instFluxToNanojansky(self.instFlux2, self.instFluxErr1)
        self.assertFloatsAlmostEqual(self.flux2, result.value)
        self.assertFloatsAlmostEqual(errFlux2, result.error)
        result = photoCalib.instFluxToNanojansky(self.instFlux2, self.instFluxErr1, self.point0)
        self.assertFloatsAlmostEqual(self.flux2, result.value)
        self.assertFloatsAlmostEqual(errFlux2, result.error)

        # test that we get a correct magnitude err for 575 nJy
        errMag2 = computeMagnitudeErr(self.instFluxErr1,
                                      self.instFlux2,
                                      calibrationErr,
                                      self.calibration,
                                      self.flux2)
        result = photoCalib.instFluxToMagnitude(self.instFlux2, self.instFluxErr1)
        self.assertFloatsAlmostEqual(self.mag2, result.value)
        self.assertFloatsAlmostEqual(errMag2, result.error)
        result = photoCalib.instFluxToMagnitude(self.instFlux2, self.instFluxErr1, self.point0)
        self.assertFloatsAlmostEqual(self.mag2, result.value)
        self.assertFloatsAlmostEqual(errMag2, result.error)

        # test calculations on a single sourceRecord
        record = self.catalog[0]
        result = photoCalib.instFluxToNanojansky(record, self.instFluxKeyName)
        self.assertEqual(self.flux1, result.value)
        self.assertFloatsAlmostEqual(errFlux1, result.error)
        result = photoCalib.instFluxToMagnitude(record, self.instFluxKeyName)
        self.assertEqual(self.mag1, result.value)
        self.assertFloatsAlmostEqual(errMag1, result.error)

        expectNanojansky = np.array([[self.flux1, errFlux1], [self.flux2, errFlux2]])
        expectMag = np.array([[self.mag1, errMag1], [self.mag2, errMag2]])
        self._testSourceCatalog(photoCalib, self.catalog, expectNanojansky, expectMag)

        # test reverse conversion: magnitude to instFlux (no position specified)
        self.assertFloatsAlmostEqual(self.instFlux1, photoCalib.magnitudeToInstFlux(self.mag1))
        self.assertFloatsAlmostEqual(self.instFlux2, photoCalib.magnitudeToInstFlux(self.mag2), rtol=1e-15)

        # test round-tripping instFlux->magnitude->instFlux (position specified)
        mag = photoCalib.instFluxToMagnitude(self.instFlux1, self.pointXShift)
        self.assertFloatsAlmostEqual(self.instFlux1,
                                     photoCalib.magnitudeToInstFlux(mag, self.pointXShift),
                                     rtol=1e-15)
        mag = photoCalib.instFluxToMagnitude(self.instFlux2, self.pointXShift)
        self.assertFloatsAlmostEqual(self.instFlux2,
                                     photoCalib.magnitudeToInstFlux(mag, self.pointXShift),
                                     rtol=1e-15)

    def _testSourceCatalog(self, photoCalib, catalog, expectNanojansky, expectMag):
        """Test passing in a sourceCatalog."""

        # test calculations on a sourceCatalog, returning the array
        result = photoCalib.instFluxToNanojansky(catalog, self.instFluxKeyName)
        self.assertFloatsAlmostEqual(expectNanojansky, result)
        result = photoCalib.instFluxToMagnitude(catalog, self.instFluxKeyName)
        self.assertFloatsAlmostEqual(expectMag, result)

        # modify the catalog in-place.
        photoCalib.instFluxToMagnitude(catalog, self.instFluxKeyName, self.instFluxKeyName)
        self.assertFloatsAlmostEqual(catalog[self.instFluxKeyName+'_mag'], expectMag[:, 0])
        self.assertFloatsAlmostEqual(catalog[self.instFluxKeyName+'_magErr'], expectMag[:, 1])

        # modify the catalog in-place
        # The original instFluxes shouldn't change: save them to test that.
        origFlux = catalog[self.instFluxKeyName+'_instFlux'].copy()
        origFluxErr = catalog[self.instFluxKeyName+'_instFluxErr'].copy()
        photoCalib.instFluxToNanojansky(catalog, self.instFluxKeyName, self.instFluxKeyName)
        self.assertFloatsAlmostEqual(catalog[self.instFluxKeyName+'_fluxErr'], expectNanojansky[:, 1])
        self.assertFloatsAlmostEqual(catalog[self.instFluxKeyName+'_flux'], expectNanojansky[:, 0])
        self.assertFloatsAlmostEqual(catalog[self.instFluxKeyName+'_instFlux'], origFlux)
        self.assertFloatsAlmostEqual(catalog[self.instFluxKeyName+'_instFluxErr'], origFluxErr)

    def testNonVarying(self):
        """Test constructing with a constant calibration factor."""
        photoCalib = lsst.afw.image.PhotoCalib(self.calibration)
        self._testPhotoCalibCenter(photoCalib, 0)

        # test on positions off the center (position should not matter)
        self.assertEqual(self.flux1, photoCalib.instFluxToNanojansky(self.instFlux1, self.pointXShift))
        self.assertEqual(self.mag1, photoCalib.instFluxToMagnitude(self.instFlux1, self.pointXShift))
        result = photoCalib.instFluxToNanojansky(self.instFlux1, self.instFluxErr1)
        self.assertEqual(self.flux1, result.value)

        photoCalib = lsst.afw.image.PhotoCalib(self.calibration, self.calibrationErr)
        self._testPhotoCalibCenter(photoCalib, self.calibrationErr)

        # test converting to a photoCalib
        photoCalib = lsst.afw.image.PhotoCalib(self.calibration, bbox=self.bbox)
        self._testPhotoCalibCenter(photoCalib, 0)

    def testConstantBoundedField(self):
        """Test constructing with a spatially-constant bounded field."""
        photoCalib = lsst.afw.image.PhotoCalib(self.constantCalibration)
        self._testPhotoCalibCenter(photoCalib, 0)

        # test on positions off the center (position should not matter)
        self.assertEqual(self.flux1, photoCalib.instFluxToNanojansky(self.instFlux1, self.pointYShift))
        self.assertEqual(self.mag1, photoCalib.instFluxToMagnitude(self.instFlux1, self.pointYShift))
        self.assertFloatsAlmostEqual(self.flux2,
                                     photoCalib.instFluxToNanojansky(self.instFlux2, self.pointXShift))
        self.assertFloatsAlmostEqual(self.mag2,
                                     photoCalib.instFluxToMagnitude(self.instFlux2, self.pointXShift))

        # test converting to a photoCalib
        photoCalib = lsst.afw.image.PhotoCalib(self.constantCalibration, self.calibrationErr)
        self._testPhotoCalibCenter(photoCalib, self.calibrationErr)

    def testLinearXBoundedField(self):
        photoCalib = lsst.afw.image.PhotoCalib(self.linearXCalibration)
        self._testPhotoCalibCenter(photoCalib, 0)

        # test on positions off the center (Y position should not matter)
        self.assertEqual(self.flux1, photoCalib.instFluxToNanojansky(self.instFlux1, self.pointYShift))
        self.assertEqual(self.mag1, photoCalib.instFluxToMagnitude(self.instFlux1, self.pointYShift))

        # test on positions off the center (X position does matter)
        calibration = (self.calibration + self.pointXShift.getX()*self.calibration/(self.bbox.getWidth()/2.))
        expect = self.instFlux1*calibration
        self.assertFloatsAlmostEqual(expect,
                                     photoCalib.instFluxToNanojansky(self.instFlux1, self.pointXShift))
        self.assertFloatsAlmostEqual((expect*u.nJy).to_value(u.ABmag),
                                     photoCalib.instFluxToMagnitude(self.instFlux1, self.pointXShift))
        expect2 = self.instFlux2*calibration
        self.assertFloatsAlmostEqual(expect2,
                                     photoCalib.instFluxToNanojansky(self.instFlux2, self.pointXShift))
        self.assertFloatsAlmostEqual((expect2*u.nJy).to_value(u.ABmag),
                                     photoCalib.instFluxToMagnitude(self.instFlux2, self.pointXShift))

        # test converting to a photoCalib
        photoCalib = lsst.afw.image.PhotoCalib(self.linearXCalibration, self.calibrationErr)
        self._testPhotoCalibCenter(photoCalib, self.calibrationErr)

        # New catalog with a spatial component in the varying direction,
        # to ensure the calculations on a catalog properly handle non-constant BF.
        # NOTE: only the first quantity of the result (nJy or mags) should change.
        catalog = self.catalog.copy(deep=True)
        catalog[0].set('centroid_x', self.pointXShift[0])
        catalog[0].set('centroid_y', self.pointXShift[1])
        errFlux1 = computeNanojanskyErr(self.instFluxErr1,
                                        self.instFlux1,
                                        self.calibrationErr,
                                        calibration,
                                        expect)
        errMag1 = computeMagnitudeErr(self.instFluxErr1,
                                      self.instFlux1,
                                      self.calibrationErr,
                                      calibration,
                                      expect)
        # re-use the same instFluxErr1 for instFlux2.
        errFlux2 = computeNanojanskyErr(self.instFluxErr1,
                                        self.instFlux2,
                                        self.calibrationErr,
                                        self.calibration,
                                        self.flux2)
        errMag2 = computeMagnitudeErr(self.instFluxErr1,
                                      self.instFlux2,
                                      self.calibrationErr,
                                      self.calibration,
                                      self.flux2)
        expectNanojansky = np.array([[expect, errFlux1], [self.flux2, errFlux2]])
        expectMag = np.array([[(expect*u.nJy).to_value(u.ABmag), errMag1], [self.mag2, errMag2]])
        self._testSourceCatalog(photoCalib, catalog, expectNanojansky, expectMag)

    def testComputeScaledCalibration(self):
        photoCalib = lsst.afw.image.PhotoCalib(self.calibration, bbox=self.bbox)
        scaledCalib = lsst.afw.image.PhotoCalib(photoCalib.computeScaledCalibration())
        self.assertEqual(1, scaledCalib.instFluxToNanojansky(self.instFlux1)*photoCalib.getCalibrationMean())
        self.assertEqual(photoCalib.instFluxToNanojansky(self.instFlux1),
                         scaledCalib.instFluxToNanojansky(self.instFlux1)*photoCalib.getCalibrationMean())

        photoCalib = lsst.afw.image.PhotoCalib(self.constantCalibration)
        scaledCalib = lsst.afw.image.PhotoCalib(photoCalib.computeScaledCalibration())

        self.assertEqual(1, scaledCalib.instFluxToNanojansky(self.instFlux1*self.calibration))
        self.assertEqual(photoCalib.instFluxToNanojansky(self.instFlux1),
                         scaledCalib.instFluxToNanojansky(self.instFlux1)*photoCalib.getCalibrationMean())

    @unittest.skip("Not yet implemented: see DM-10154")
    def testComputeScalingTo(self):
        photoCalib1 = lsst.afw.image.PhotoCalib(self.calibration, self.calibrationErr, bbox=self.bbox)
        photoCalib2 = lsst.afw.image.PhotoCalib(self.calibration*500, self.calibrationErr, bbox=self.bbox)
        scaling = photoCalib1.computeScalingTo(photoCalib2)(self.pointXShift)
        self.assertEqual(photoCalib1.instFluxToNanojansky(self.instFlux1, self.pointXShift)*scaling,
                         photoCalib2.instFluxToNanojansky(self.instFlux1, self.pointXShift))

        photoCalib3 = lsst.afw.image.PhotoCalib(self.constantCalibration, self.calibrationErr)
        scaling = photoCalib1.computeScalingTo(photoCalib3)(self.pointXShift)
        self.assertEqual(photoCalib1.instFluxToNanojansky(self.instFlux1, self.pointXShift)*scaling,
                         photoCalib3.instFluxToNanojansky(self.instFlux1, self.pointXShift))
        scaling = photoCalib3.computeScalingTo(photoCalib1)(self.pointXShift)
        self.assertEqual(photoCalib3.instFluxToNanojansky(self.instFlux1, self.pointXShift)*scaling,
                         photoCalib1.instFluxToNanojansky(self.instFlux1, self.pointXShift))

        photoCalib4 = lsst.afw.image.PhotoCalib(self.linearXCalibration, self.calibrationErr)
        scaling = photoCalib1.computeScalingTo(photoCalib4)(self.pointXShift)
        self.assertEqual(photoCalib1.instFluxToNanojansky(self.instFlux1, self.pointXShift)*scaling,
                         photoCalib4.instFluxToNanojansky(self.instFlux1, self.pointXShift))
        scaling = photoCalib4.computeScalingTo(photoCalib1)(self.pointXShift)
        self.assertEqual(photoCalib4.instFluxToNanojansky(self.instFlux1, self.pointXShift)*scaling,
                         photoCalib1.instFluxToNanojansky(self.instFlux1, self.pointXShift))

        # Don't allow division of BoundedFields with different bounding boxes
        photoCalibNoBBox = lsst.afw.image.PhotoCalib(self.calibration, self.calibrationErr)
        with self.assertRaises(lsst.pex.exceptions.DomainError):
            scaling = photoCalibNoBBox.computeScalingTo(photoCalib1)
        with self.assertRaises(lsst.pex.exceptions.DomainError):
            scaling = photoCalibNoBBox.computeScalingTo(photoCalib4)
        with self.assertRaises(lsst.pex.exceptions.DomainError):
            scaling = photoCalib1.computeScalingTo(photoCalibNoBBox)

    def _testPersistence(self, photoCalib):
        with lsst.utils.tests.getTempFilePath(".fits") as filename:
            photoCalib.writeFits(filename)
            result = lsst.afw.image.PhotoCalib.readFits(filename)
            self.assertEqual(result, photoCalib)

    def testPersistence(self):
        photoCalib = lsst.afw.image.PhotoCalib(self.calibration)
        self._testPersistence(photoCalib)

        photoCalib = lsst.afw.image.PhotoCalib(self.calibration, self.calibrationErr)
        self._testPersistence(photoCalib)

        photoCalib = lsst.afw.image.PhotoCalib(self.calibration, self.calibrationErr, self.bbox)
        self._testPersistence(photoCalib)

        photoCalib = lsst.afw.image.PhotoCalib(self.constantCalibration, self.calibrationErr)
        self._testPersistence(photoCalib)

        photoCalib = lsst.afw.image.PhotoCalib(self.linearXCalibration, self.calibrationErr)
        self._testPersistence(photoCalib)

    def testPersistenceVersions(self):
        """Test that different versions are handled appropriately."""
        # the values that were persisted in this photoCalib
        mean = 123
        err = 45
        dataDir = os.path.join(os.path.split(__file__)[0], "data")

        # implicit version 0
        filePath = os.path.join(dataDir, "photoCalib-noversion.fits")
        photoCalib = lsst.afw.image.PhotoCalib.readFits(filePath)
        self.assertEqual(photoCalib.getCalibrationMean(), mean)
        self.assertEqual(photoCalib.getCalibrationErr(), err)

        # explicit version 0
        filePath = os.path.join(dataDir, "photoCalib-version0.fits")
        photoCalib = lsst.afw.image.PhotoCalib.readFits(filePath)
        self.assertEqual(photoCalib.getCalibrationMean(), mean)
        self.assertEqual(photoCalib.getCalibrationErr(), err)

    def testCalibEquality(self):
        photoCalib1 = lsst.afw.image.PhotoCalib(self.linearXCalibration, 0.5)
        photoCalib2 = lsst.afw.image.PhotoCalib(self.linearXCalibration, 0.5)
        photoCalib3 = lsst.afw.image.PhotoCalib(5, 0.5)
        photoCalib4 = lsst.afw.image.PhotoCalib(5, 0.5)
        photoCalib5 = lsst.afw.image.PhotoCalib(5)
        photoCalib6 = lsst.afw.image.PhotoCalib(self.linearXCalibration)
        photoCalib7 = lsst.afw.image.PhotoCalib(self.calibration, 0.5)
        photoCalib8 = lsst.afw.image.PhotoCalib(self.constantCalibration, 0.5)

        constantCalibration = lsst.afw.math.ChebyshevBoundedField(self.bbox, np.array([[self.calibration]]))
        photoCalib9 = lsst.afw.image.PhotoCalib(constantCalibration, 0.5)

        self.assertEqual(photoCalib1, photoCalib1)
        self.assertEqual(photoCalib1, photoCalib2)
        self.assertEqual(photoCalib3, photoCalib4)
        self.assertEqual(photoCalib8, photoCalib9)

        self.assertNotEqual(photoCalib1, photoCalib6)
        self.assertNotEqual(photoCalib1, photoCalib7)
        self.assertNotEqual(photoCalib1, photoCalib3)
        self.assertNotEqual(photoCalib3, photoCalib5)
        self.assertNotEqual(photoCalib1, photoCalib8)

        self.assertFalse(photoCalib1 != photoCalib2)  # using assertFalse to directly test != operator

    def setupImage(self):
        dim = (5, 6)
        npDim = (dim[1], dim[0])  # numpy and afw have a different x/y order
        sigma = 10.0
        image = np.random.normal(loc=1000.0, scale=sigma, size=npDim).astype(np.float32)
        mask = np.zeros(npDim, dtype=np.int32)
        variance = (np.random.normal(loc=0.0, scale=sigma, size=npDim).astype(np.float32))**2
        maskedImage = lsst.afw.image.basicUtils.makeMaskedImageFromArrays(image, mask, variance)
        maskedImage.mask[0, 0] = True  # set one mask bit to check propagation of mask bits.

        return npDim, maskedImage, image, mask, variance

    def testCalibrateImageConstant(self):
        """Test a spatially-constant calibration."""
        npDim, maskedImage, image, mask, variance = self.setupImage()
        outImage = maskedImage.image.getArray()*self.calibration
        expect = makeCalibratedMaskedImage(image, mask, variance, outImage,
                                           self.calibration, self.calibrationErr)
        photoCalib = lsst.afw.image.PhotoCalib(self.calibration, self.calibrationErr)
        result = photoCalib.calibrateImage(maskedImage)
        self.assertMaskedImagesAlmostEqual(expect, result)

        # same test, but without using the calibration error
        expect = makeCalibratedMaskedImageNoCalibrationError(image, mask, variance, outImage)
        result = photoCalib.calibrateImage(maskedImage, includeScaleUncertainty=False)
        self.assertMaskedImagesAlmostEqual(expect, result)

    def testCalibrateImageNonConstant(self):
        """Test a spatially-varying calibration."""
        npDim, maskedImage, image, mask, variance = self.setupImage()
        xIndex, yIndex = np.indices(npDim, dtype=np.float64)
        # y then x, as afw order and np order are flipped
        calibration = self.linearXCalibration.evaluate(yIndex.flatten(), xIndex.flatten()).reshape(npDim)
        outImage = maskedImage.image.getArray()*calibration  # element-wise product, not matrix product
        expect = makeCalibratedMaskedImage(image, mask, variance, outImage, calibration, self.calibrationErr)

        photoCalib = lsst.afw.image.PhotoCalib(self.linearXCalibration, self.calibrationErr)
        result = photoCalib.calibrateImage(maskedImage)
        self.assertMaskedImagesAlmostEqual(expect, result)

        # same test, but without using the calibration error
        expect = makeCalibratedMaskedImageNoCalibrationError(image, mask, variance, outImage)
        result = photoCalib.calibrateImage(maskedImage, includeScaleUncertainty=False)
        self.assertMaskedImagesAlmostEqual(expect, result)

    def testCalibrateImageNonConstantSubimage(self):
        """Test a non-constant calibration on a sub-image, to ensure we're
        handling xy0 correctly.
        """
        npDim, maskedImage, image, mask, variance = self.setupImage()
        xIndex, yIndex = np.indices(npDim, dtype=np.float64)
        calibration = self.linearXCalibration.evaluate(yIndex.flatten(), xIndex.flatten()).reshape(npDim)

        outImage = maskedImage.image.getArray()*calibration  # element-wise product, not matrix product
        expect = makeCalibratedMaskedImage(image, mask, variance, outImage, calibration, self.calibrationErr)

        subBox = lsst.geom.Box2I(lsst.geom.Point2I(2, 4), lsst.geom.Point2I(4, 5))
        subImage = maskedImage.subset(subBox)
        photoCalib = lsst.afw.image.PhotoCalib(self.linearXCalibration, self.calibrationErr)
        result = photoCalib.calibrateImage(subImage)
        self.assertMaskedImagesAlmostEqual(expect.subset(subBox), result)

        # same test, but without using the calibration error
        expect = makeCalibratedMaskedImageNoCalibrationError(image, mask, variance, outImage)
        result = photoCalib.calibrateImage(maskedImage, includeScaleUncertainty=False)
        self.assertMaskedImagesAlmostEqual(expect, result)

    def testNonPositiveMeans(self):
        # no negative calibrations
        with(self.assertRaises(lsst.pex.exceptions.InvalidParameterError)):
            lsst.afw.image.PhotoCalib(-1.0)
        # no negative errors
        with(self.assertRaises(lsst.pex.exceptions.InvalidParameterError)):
            lsst.afw.image.PhotoCalib(1.0, -1.0)

        # no negative calibration mean when computed from the bounded field
        negativeCalibration = lsst.afw.math.ChebyshevBoundedField(self.bbox,
                                                                  np.array([[-self.calibration]]))
        with(self.assertRaises(lsst.pex.exceptions.InvalidParameterError)):
            lsst.afw.image.PhotoCalib(negativeCalibration)
        # no negative calibration error
        with(self.assertRaises(lsst.pex.exceptions.InvalidParameterError)):
            lsst.afw.image.PhotoCalib(self.constantCalibration, -1.0)

        # no negative explicit calibration mean
        with(self.assertRaises(lsst.pex.exceptions.InvalidParameterError)):
            lsst.afw.image.PhotoCalib(-1.0, 0, self.constantCalibration, True)
        # no negative calibration error
        with(self.assertRaises(lsst.pex.exceptions.InvalidParameterError)):
            lsst.afw.image.PhotoCalib(1.0, -1.0, self.constantCalibration, True)

    def testPositiveErrors(self):
        """The errors should always be positive, regardless of whether the
        input flux is negative (as can happen in difference imaging).
        This tests and fixes tickets/DM-16696.
        """
        photoCalib = lsst.afw.image.PhotoCalib(self.calibration)
        result = photoCalib.instFluxToNanojansky(-100, 10)
        self.assertGreater(result.error, 0)


class MemoryTester(lsst.utils.tests.MemoryTestCase):
    pass


def setup_module(module):
    lsst.utils.tests.init()


if __name__ == "__main__":
    lsst.utils.tests.init()
    unittest.main()
