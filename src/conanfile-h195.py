import os
from conans import ConanFile
from conanfile_base import PalladioConanBase


class PalladioConan(PalladioConanBase):
    houdini_version = ">19.5.0 <20.0.0"
