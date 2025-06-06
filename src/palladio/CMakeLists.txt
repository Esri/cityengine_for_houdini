cmake_minimum_required(VERSION 3.13)


### target definition

add_library(${TGT_PALLADIO} SHARED
        PalladioMain.cpp
        ModelConverter.cpp
        Utils.cpp
        ShapeConverter.cpp
        ShapeData.cpp
        ShapeGenerator.cpp
        NodeParameter.cpp
        NodeSpareParameter.cpp
        PRTContext.cpp
        ResolveMapCache.cpp
        RuleAttributes.cpp
        SOPAssign.cpp
        SOPGenerate.cpp
        PrimitivePartition.cpp
        AttrEvalCallbacks.cpp
        AttributeConversion.cpp
		AnnotationParsing.cpp
        MultiWatch.cpp
        PrimitiveClassifier.cpp
        LogHandler.cpp
        LRUCache.h
        HoleConverter.cpp)

get_target_property(CODEC_SOURCE_DIR ${TGT_CODEC} SOURCE_DIR)
target_include_directories(${TGT_PALLADIO} PRIVATE
        ${CODEC_SOURCE_DIR}
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>)


### compiler settings

pld_set_common_compiler_flags(${TGT_PALLADIO})
pld_add_version_definitions(${TGT_PALLADIO})

if (PLD_TEST)
    message(STATUS "Enabling test exports...")
    target_compile_definitions(palladio PRIVATE -DPLD_TEST_EXPORTS)
endif ()

if (PLD_LINUX)
    # suppress GCC warnings triggered within HDK
    target_compile_options(${TGT_PALLADIO} PRIVATE -Wno-stringop-overread -Wno-free-nonheap-object)
    target_link_options(${TGT_PALLADIO} PRIVATE -Wno-stringop-overread -Wno-free-nonheap-object)

    set_target_properties(${TGT_PALLADIO} PROPERTIES
            INSTALL_RPATH "\$ORIGIN/../packages/palladio"
            INSTALL_RPATH_USE_LINK_PATH FALSE
            SKIP_RPATH FALSE
            BUILD_WITH_INSTALL_RPATH TRUE)
endif ()


### dependencies

pld_add_dependency_prt(${TGT_PALLADIO})
pld_add_dependency_houdini(${TGT_PALLADIO})


### setup install target

install(TARGETS ${TGT_PALLADIO}
        RUNTIME DESTINATION ${HOUDINI_RELATIVE_DSO_PATH}
        LIBRARY DESTINATION ${HOUDINI_RELATIVE_DSO_PATH})

install(DIRECTORY ${PROJECT_SOURCE_DIR}/../doc DESTINATION ${HOUDINI_RELATIVE_PALLADIO_PATH})
install(FILES ${PROJECT_SOURCE_DIR}/../README.md ${PROJECT_SOURCE_DIR}/../LICENSE DESTINATION ${HOUDINI_RELATIVE_PALLADIO_PATH})
install(FILES ${PROJECT_SOURCE_DIR}/palladio.json DESTINATION ${HOUDINI_RELATIVE_PACKAGES_PATH})
install(DIRECTORY ${PROJECT_SOURCE_DIR}/icons/ DESTINATION ${HOUDINI_RELATIVE_ICON_PATH})