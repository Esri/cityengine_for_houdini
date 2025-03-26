# escape=`

ARG BASE_IMAGE="python:3.12-windowsservercore-1809"

FROM ${BASE_IMAGE}
SHELL ["cmd", "/S", "/C"]

# see https://learn.microsoft.com/en-us/visualstudio/install/build-tools-container?view=vs-2022
ADD https://aka.ms/vs/17/release/vs_buildtools.exe C:/temp/vs_buildtools.exe
RUN C:\temp\vs_buildtools.exe --quiet --wait --norestart --nocache ^ `
    --installPath C:\BuildTools ^ `
    --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended ^ `
    --add Microsoft.VisualStudio.Component.VC.14.37.17.7.x86.x64 ^ `
    || IF "%ERRORLEVEL%"=="3010" EXIT 0

WORKDIR C:/tmp/work

RUN python -m pip install --no-cache-dir --upgrade pip wheel "conan<2.0"
RUN conan remote add zrh-conan http://zrh-conan.esri.com:8081/artifactory/api/conan/conan-local

ENTRYPOINT [ "C:\\BuildTools\\VC\\Auxiliary\\Build\\vcvarsall.bat", "x64", "-vcvars_ver=14.37", "&&" ]
