#/usr/bin/sh
cd build
/Applications/CMake.app/Contents/bin/cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX=build/install -DBUILD_SHARED_LIBS=OFF
make -j 16
#make install
echo Copy to testbed project...
cp src/lib/Iex/libIex-3_1.a ~/proj/aras/image-formats-testbed/src/openexr/lib
cp src/lib/IlmThread/libIlmThread-3_1.a ~/proj/aras/image-formats-testbed/src/openexr/lib
cp src/lib/OpenEXR/libOpenEXR-3_1.a ~/proj/aras/image-formats-testbed/src/openexr/lib
cp src/lib/OpenEXRCore/libOpenEXRCore-3_1.a ~/proj/aras/image-formats-testbed/src/openexr/lib
cp src/lib/OpenEXRUtil/libOpenEXRUtil-3_1.a ~/proj/aras/image-formats-testbed/src/openexr/lib
cp ../src/lib/OpenEXR/ImfCompression.h ~/proj/aras/image-formats-testbed/src/openexr/include/OpenEXR
cp ../src/lib/OpenEXR/ImfStandardAttributes.h ~/proj/aras/image-formats-testbed/src/openexr/include/OpenEXR
