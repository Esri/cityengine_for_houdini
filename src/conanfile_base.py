import os
from conans import ConanFile


class PalladioConanBase(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "cmake"

    cesdk_default_version = "3.2.10650"
    houdini_version = "" # see subclasses
    catch2_version = "2.13.7"

    def requirements(self):
        self.requires(f"catch2/{self.catch2_version}")

        if "PLD_CONAN_HOUDINI_VERSION" in os.environ:
            self.requires(f'houdini/{os.environ["PLD_CONAN_HOUDINI_VERSION"]}@sidefx/stable')
        else:
            self.requires(f"houdini/[{self.houdini_version}]@sidefx/stable")

        if "PLD_CONAN_SKIP_CESDK" not in os.environ:
            if "PLD_CONAN_CESDK_VERSION" in os.environ:
                cesdk_version = os.environ["PLD_CONAN_CESDK_VERSION"]
            else:
                cesdk_version = self.cesdk_default_version
            self.requires(f"cesdk/{cesdk_version}@esri-rd-zurich/stable")
