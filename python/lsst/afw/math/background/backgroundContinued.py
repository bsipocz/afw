# This file is part of afw.
#
# Developed for the LSST Data Management System.
# This product includes software developed by the LSST Project
# (https://www.lsst.org).
# See the COPYRIGHT file at the top-level directory of this distribution
# for details of code ownership.
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
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

import warnings

from lsst.utils import continueClass
from lsst.utils.deprecated import deprecate_pybind11
from .background import Background, BackgroundControl, BackgroundMI
from ..interpolate import Interpolate

__all__ = []  # import this module only for its side effects


@continueClass  # noqa: F811
class BackgroundControl:
    _init = BackgroundControl.__init__

    def __init__(self, *args, **kwargs):
        # This constructor called dozens of times; warn only for invalid use
        if (args and (isinstance(args[0], str) or isinstance(args[0], Interpolate.Style))) \
                or "style" in kwargs:
            warnings.warn('Call to deprecated method __init__. (Overloads that take a ``style`` parameter '
                          'are deprecated; the style must be passed to `Background.getImageF` instead. '
                          'To be removed after 20.0.0.)',
                          FutureWarning, stacklevel=2)
        self._init(*args, **kwargs)


@continueClass  # noqa: F811
class Background:
    def __reduce__(self):
        """Pickling"""
        return self.__class__, (self.getImageBBox(), self.getStatsImage())

    _getImageF = Background.getImageF

    def getImageF(self, *args, **kwargs):
        # This method called hundreds of times; warn only for invalid use
        if not args and not kwargs:
            warnings.warn('Call to deprecated method getImageF(). (Zero-argument overload is deprecated; '
                          'use one that takes an ``interpStyle`` instead. To be removed after 20.0.0.)',
                          FutureWarning, stacklevel=2)
        return self._getImageF(*args, **kwargs)


BackgroundControl.getInterpStyle = deprecate_pybind11(
    BackgroundControl.getInterpStyle,
    reason='Replaced by passing style to `Background.getImageF`. To be removed after 20.0.0.')
BackgroundControl.setInterpStyle = deprecate_pybind11(
    BackgroundControl.setInterpStyle,
    reason='Replaced by passing style to `Background.getImageF`. To be removed after 20.0.0.')
BackgroundMI.getPixel = deprecate_pybind11(
    BackgroundMI.getPixel,
    reason='Use `getImageF` instead. To be removed after 20.0.0.')
