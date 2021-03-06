set(KIT qSlicer${MODULE_NAME}Module)

set(KIT_TEST_SRCS
  vtkSlicerSegmentComparisonModuleLogicTest1.cxx
  )

slicerMacroConfigureModuleCxxTestDriver(
  NAME ${KIT}
  SOURCES ${KIT_TEST_SRCS}
  TARGET_LIBRARIES vtkSlicerSegmentComparisonModuleLogic vtkSlicerDicomRtImportExportConversionRules
  WITH_VTK_DEBUG_LEAKS_CHECK
  )

#-----------------------------------------------------------------------------
set(TEMP "${CMAKE_BINARY_DIR}/Testing/Temporary")

macro(TEST_WITH_DATA TestName TestExecutableName
      DataDirectoryPath InputSegmentationReferenceFile InputSegmentationCompareFile
      TemporarySceneFile ApplySimpleTransformToInputCompare
      HausdorffMaximumMm HausdorffAverageMm Hausdorff95PercentMm
      DiceCoefficient TruePositivesPercent TrueNegativesPercent
      FalsePositivesPercent FalseNegativesPercent)
  add_test(
    NAME ${TestName}
    COMMAND ${Slicer_LAUNCH_COMMAND} $<TARGET_FILE:${KIT}CxxTests> ${TestExecutableName} ${ARGN}
    -DataDirectoryPath ${DataDirectoryPath}
    -InputSegmentationReferenceFile ${InputSegmentationReferenceFile}
    -InputSegmentationCompareFile ${InputSegmentationCompareFile}
    -TemporarySceneFile ${TemporarySceneFile}
    -ApplySimpleTransformToInputCompare ${ApplySimpleTransformToInputCompare}
    -HausdorffMaximumMm ${HausdorffMaximumMm}
    -HausdorffAverageMm ${HausdorffAverageMm}
    -Hausdorff95PercentMm ${Hausdorff95PercentMm}
    -DiceCoefficient ${DiceCoefficient}
    -TruePositivesPercent ${TruePositivesPercent}
    -TrueNegativesPercent ${TrueNegativesPercent}
    -FalsePositivesPercent ${FalsePositivesPercent}
    -FalseNegativesPercent ${FalseNegativesPercent}
  )
endmacro()

#-----------------------------------------------------------------------------
TEST_WITH_DATA(
  vtkSlicerSegmentComparisonModuleLogicTest_EclipseProstate_Base
  vtkSlicerSegmentComparisonModuleLogicTest1
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../Testing/Data/
  EclipseProstate_Rectum.seg.vtm
  EclipseProstate_Expanded_5_5_5_Rectum.seg.nrrd
  ${TEMP}/TestScene_SegmentComparison_EclipseProstate.mrml
  0
  8.34375
  5.17621
  6.07853
  0.542084
  11.2075
  69.8579
  18.9346
  0.0
)
set_tests_properties(vtkSlicerSegmentComparisonModuleLogicTest_EclipseProstate_Base PROPERTIES FAIL_REGULAR_EXPRESSION "Error;ERROR;Warning;WARNING" )

#-----------------------------------------------------------------------------
TEST_WITH_DATA(
  vtkSlicerSegmentComparisonModuleLogicTest_EclipseProstate_SameInput
  vtkSlicerSegmentComparisonModuleLogicTest1
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../Testing/Data/
  EclipseProstate_Rectum.seg.vtm
  EclipseProstate_Rectum.seg.vtm
  ${TEMP}/TestScene_SegmentComparison_EclipseProstate_SameInput.mrml
  0
  0.0
  0.0
  0.0
  1
  22.528
  77.472
  0.0
  0.0
)
set_tests_properties(vtkSlicerSegmentComparisonModuleLogicTest_EclipseProstate_SameInput PROPERTIES FAIL_REGULAR_EXPRESSION "Error;ERROR;Warning;WARNING" )

#-----------------------------------------------------------------------------
TEST_WITH_DATA(
  vtkSlicerSegmentComparisonModuleLogicTest_EclipseProstate_Transformed
  vtkSlicerSegmentComparisonModuleLogicTest1
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../Testing/Data/
  EclipseProstate_Rectum.seg.vtm
  EclipseProstate_Rectum.seg.vtm
  ${TEMP}/TestScene_SegmentComparison_EclipseProstate_Transformed.mrml
  1
  4.88281
  2.13749
  4.68861
  0.700964
  13.431
  75.1094
  5.72977
  5.72977
)
set_tests_properties(vtkSlicerSegmentComparisonModuleLogicTest_EclipseProstate_Transformed PROPERTIES FAIL_REGULAR_EXPRESSION "Error;ERROR;Warning;WARNING" )
