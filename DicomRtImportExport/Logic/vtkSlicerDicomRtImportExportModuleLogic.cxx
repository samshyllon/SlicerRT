/*==============================================================================

  Copyright (c) Laboratory for Percutaneous Surgery (PerkLab)
  Queen's University, Kingston, ON, Canada and
  Radiation Medicine Program, University Health Network,
  Princess Margaret Hospital, Toronto, ON, Canada. All Rights Reserved.

  See COPYRIGHT.txt
  or http://www.slicer.org/copyright/copyright.txt for details.

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  This file was originally developed by Kevin Wang, Radiation Medicine Program,
  University Health Network and Csaba Pinter, PerkLab, Queen's University and
  Andras Lasso, PerkLab, Queen's University, and was supported by Cancer Care
  Ontario (CCO)'s ACRU program with funds provided by the Ontario Ministry of
  Health and Long-Term Care and Ontario Consortium for Adaptive Interventions in
  Radiation Oncology (OCAIRO).

==============================================================================*/

// DicomRtImportExport includes
#include "vtkSlicerDicomRtImportExportModuleLogic.h"
#include "vtkSlicerDicomRtReader.h"
#include "vtkSlicerDicomRtWriter.h"
#include "vtkRibbonModelToBinaryLabelmapConversionRule.h"
#include "vtkPlanarContourToRibbonModelConversionRule.h"
#include "vtkPlanarContourToClosedSurfaceConversionRule.h"
#include "vtkClosedSurfaceToFractionalLabelmapConversionRule.h"
#include "vtkFractionalLabelmapToClosedSurfaceConversionRule.h"

// CTK includes
#include <ctkDICOMDatabase.h>

// Qt includes
#include <QSettings>
#include "qSlicerApplication.h"

// SubjectHierarchy includes
#include "vtkMRMLSubjectHierarchyConstants.h"
#include "vtkMRMLSubjectHierarchyNode.h"
#include "vtkSlicerSubjectHierarchyModuleLogic.h"

// SlicerRT includes
#include "SlicerRtCommon.h"
#include "PlmCommon.h"
#include "vtkMRMLIsodoseNode.h"
#include "vtkMRMLPlanarImageNode.h"
#include "vtkSlicerIsodoseModuleLogic.h"
#include "vtkSlicerPlanarImageModuleLogic.h"
#include "vtkSlicerBeamsModuleLogic.h"
#include "vtkMRMLRTPlanNode.h"
#include "vtkMRMLRTBeamNode.h"

// Segmentations includes
#include "vtkMRMLSegmentationNode.h"
#include "vtkMRMLSegmentationDisplayNode.h"
#include "vtkMRMLSegmentationStorageNode.h"
#include "vtkSlicerSegmentationsModuleLogic.h"

// vtkSegmentationCore includes
#include "vtkOrientedImageDataResample.h"
#include "vtkSegmentationConverterFactory.h"

// DCMTK includes
#include <dcmtk/dcmdata/dcfilefo.h>
#include <dcmtk/dcmdata/dcdeftag.h>
#include <dcmtk/dcmdata/dcdatset.h>
#include <dcmtk/dcmdata/dcuid.h>
#include <dcmtk/ofstd/ofcond.h>
#include <dcmtk/ofstd/ofstring.h>
#include <dcmtk/ofstd/ofstd.h> // for class OFStandard
#include <dcmtk/dcmrt/drtdose.h>
#include <dcmtk/dcmrt/drtimage.h>
#include <dcmtk/dcmrt/drtplan.h>
#include <dcmtk/dcmrt/drtstrct.h>

// MRML includes
#include <vtkMRMLColorTableNode.h>
#include <vtkMRMLModelDisplayNode.h>
#include <vtkMRMLModelHierarchyNode.h>
#include <vtkMRMLModelNode.h>
#include <vtkMRMLScalarVolumeNode.h>
#include <vtkMRMLScalarVolumeDisplayNode.h>
#include <vtkMRMLLabelMapVolumeNode.h>
#include <vtkMRMLLabelMapVolumeDisplayNode.h>
#include <vtkMRMLSelectionNode.h>
#include <vtkMRMLVolumeArchetypeStorageNode.h>

// Markups includes
#include <vtkMRMLMarkupsFiducialNode.h>
#include <vtkMRMLMarkupsDisplayNode.h>

// VTK includes
#include <vtkSmartPointer.h>
#include <vtkPolyData.h>
#include <vtkImageData.h>
#include <vtkLookupTable.h>
#include <vtkImageCast.h>
#include <vtkStringArray.h>
#include <vtkObjectFactory.h>
#include <vtkGeneralTransform.h>
#include <vtkTransformPolyDataFilter.h>
#include <vtkCutter.h>
#include <vtkStripper.h>
#include <vtkPlane.h>

// ITK includes
#include <itkImage.h>

// DICOMLib includes
#include "vtkSlicerDICOMLoadable.h"
#include "vtkSlicerDICOMExportable.h"

//----------------------------------------------------------------------------
vtkStandardNewMacro(vtkSlicerDicomRtImportExportModuleLogic);
vtkCxxSetObjectMacro(vtkSlicerDicomRtImportExportModuleLogic, IsodoseLogic, vtkSlicerIsodoseModuleLogic);
vtkCxxSetObjectMacro(vtkSlicerDicomRtImportExportModuleLogic, PlanarImageLogic, vtkSlicerPlanarImageModuleLogic);
vtkCxxSetObjectMacro(vtkSlicerDicomRtImportExportModuleLogic, BeamsLogic, vtkSlicerBeamsModuleLogic);

//----------------------------------------------------------------------------
class vtkSlicerDicomRtImportExportModuleLogic::vtkInternal
{
public:
  vtkInternal(vtkSlicerDicomRtImportExportModuleLogic* external);
  ~vtkInternal() { };

  /// Examine RT Dose dataset and assemble name and referenced SOP instances
  void ExamineRtDoseDataset(DcmDataset* dataset, OFString &name, std::vector<OFString> &referencedSOPInstanceUIDs);

  /// Examine RT Plan dataset and assemble name and referenced SOP instances
  void ExamineRtPlanDataset(DcmDataset* dataset, OFString &name, std::vector<OFString> &referencedSOPInstanceUIDs);

  /// Examine RT Structure Set dataset and assemble name and referenced SOP instances
  void ExamineRtStructureSetDataset(DcmDataset* dataset, OFString &name, std::vector<OFString> &referencedSOPInstanceUIDs);

  /// Examine RT Image dataset and assemble name and referenced SOP instances
  void ExamineRtImageDataset(DcmDataset* dataset, OFString &name, std::vector<OFString> &referencedSOPInstanceUIDs);

  /// Load RT Dose and related objects into the MRML scene
  /// \return Success flag
  bool LoadRtDose(vtkSlicerDicomRtReader* rtReader, vtkSlicerDICOMLoadable* loadable);

  /// Load RT Plan and related objects into the MRML scene
  /// \return Success flag
  bool LoadRtPlan(vtkSlicerDicomRtReader* rtReader, vtkSlicerDICOMLoadable* loadable);

  /// Load RT Structure Set and related objects into the MRML scene
  /// \return Success flag
  bool LoadRtStructureSet(vtkSlicerDicomRtReader* rtReader, vtkSlicerDICOMLoadable* loadable);

  /// Load RT Image and related objects into the MRML scene
  /// \return Success flag
  bool LoadRtImage(vtkSlicerDicomRtReader* rtReader, vtkSlicerDICOMLoadable* loadable);

  /// Add an ROI point to the scene
  vtkMRMLMarkupsFiducialNode* AddRoiPoint(double* roiPosition, std::string baseName, double* roiColor);

  /// Insert currently loaded series in the proper place in subject hierarchy
  void InsertSeriesInSubjectHierarchy(vtkSlicerDicomRtReader* rtReader);

  /// Compute and set geometry of an RT image
  /// \param node Either the volume node of the loaded RT image, or the isocenter fiducial node (corresponding to an RT image). This function is called both when
  ///    loading an RT image and when loading a beam. Sets up the RT image geometry only if both information (the image itself and the isocenter data) are available
  void SetupRtImageGeometry(vtkMRMLNode* node);

public:
  vtkSlicerDicomRtImportExportModuleLogic* External;
};

//----------------------------------------------------------------------------
// vtkInternal methods

//----------------------------------------------------------------------------
vtkSlicerDicomRtImportExportModuleLogic::vtkInternal::vtkInternal(vtkSlicerDicomRtImportExportModuleLogic* external)
  : External(external)
{
}

//-----------------------------------------------------------------------------
void vtkSlicerDicomRtImportExportModuleLogic::vtkInternal::ExamineRtDoseDataset(DcmDataset* dataset, OFString &name, std::vector<OFString> &referencedSOPInstanceUIDs)
{
  if (!dataset)
    {
    return;
    }

  // Assemble name
  name += "RTDOSE";
  OFString instanceNumber;
  dataset->findAndGetOFString(DCM_InstanceNumber, instanceNumber);
  OFString seriesDescription;
  dataset->findAndGetOFString(DCM_SeriesDescription, seriesDescription);
  if (!seriesDescription.empty())
  {
    name += ": " + seriesDescription;
  }
  if (!instanceNumber.empty())
  {
    name += " [" + instanceNumber + "]";
  }

  // Find RTPlan name for RTDose series
  OFString referencedSOPInstanceUID("");
  DRTDoseIOD rtDoseObject;
  if (rtDoseObject.read(*dataset).good())
  {
    DRTReferencedRTPlanSequence &referencedRTPlanSequence = rtDoseObject.getReferencedRTPlanSequence();
    if (referencedRTPlanSequence.gotoFirstItem().good())
    {
      DRTReferencedRTPlanSequence::Item &referencedRTPlanSequenceItem = referencedRTPlanSequence.getCurrentItem();
      if (referencedRTPlanSequenceItem.isValid())
      {
        if (referencedRTPlanSequenceItem.getReferencedSOPInstanceUID(referencedSOPInstanceUID).good())
        {
          referencedSOPInstanceUIDs.push_back(referencedSOPInstanceUID);
        }
      }
    }
  }

  // Create and open DICOM database to perform database operations for getting RTPlan name
  QSettings settings;
  QString databaseDirectory = settings.value("DatabaseDirectory").toString();
  QString databaseFile = databaseDirectory + vtkSlicerDicomRtReader::DICOMRTREADER_DICOM_DATABASE_FILENAME.c_str();
  ctkDICOMDatabase* dicomDatabase = new ctkDICOMDatabase();
  dicomDatabase->openDatabase(databaseFile, vtkSlicerDicomRtReader::DICOMRTREADER_DICOM_CONNECTION_NAME.c_str());

  // Get RTPlan name to show it with the dose
  QString rtPlanLabelTag("300a,0002");
  QString rtPlanFileName = dicomDatabase->fileForInstance(referencedSOPInstanceUID.c_str());
  if (!rtPlanFileName.isEmpty())
  {
    name += OFString(": ") + OFString(dicomDatabase->fileValue(rtPlanFileName,rtPlanLabelTag).toLatin1().constData());
  }

  // Close and delete DICOM database
  dicomDatabase->closeDatabase();
  delete dicomDatabase;
  QSqlDatabase::removeDatabase(vtkSlicerDicomRtReader::DICOMRTREADER_DICOM_CONNECTION_NAME.c_str());
  QSqlDatabase::removeDatabase(QString(vtkSlicerDicomRtReader::DICOMRTREADER_DICOM_CONNECTION_NAME.c_str()) + "TagCache");
}

//-----------------------------------------------------------------------------
void vtkSlicerDicomRtImportExportModuleLogic::vtkInternal::ExamineRtPlanDataset(DcmDataset* dataset, OFString &name, std::vector<OFString> &referencedSOPInstanceUIDs)
{
  if (!dataset)
    {
    return;
    }

  // Assemble name
  name += "RTPLAN";
  OFString planLabel;
  dataset->findAndGetOFString(DCM_RTPlanLabel, planLabel);
  OFString planName;
  dataset->findAndGetOFString(DCM_RTPlanName, planName);
  if (!planLabel.empty() && !planName.empty())
  {
    if (planLabel.compare(planName)!=0)
    {
      // Plan label and name is different, display both
      name += ": " + planLabel + " (" + planName + ")";
    }
    else
    {
      name += ": " + planLabel;
    }
  }
  else if (!planLabel.empty() && planName.empty())
  {
    name += ": " + planLabel;
  }
  else if (planLabel.empty() && !planName.empty())
  {
    name += ": " + planName;
  }
}

//-----------------------------------------------------------------------------
void vtkSlicerDicomRtImportExportModuleLogic::vtkInternal::ExamineRtStructureSetDataset(DcmDataset* dataset, OFString &name, std::vector<OFString> &referencedSOPInstanceUIDs)
{
  if (!dataset)
    {
    return;
    }

  // Assemble name
  name += "RTSTRUCT";
  OFString structLabel;
  dataset->findAndGetOFString(DCM_StructureSetLabel, structLabel);
  if (!structLabel.empty())
  {
    name += ": " + structLabel;
  }

  // Get referenced image instance UIDs
  OFString referencedSOPInstanceUID("");
  DRTStructureSetIOD rtStructureSetObject;
  if (rtStructureSetObject.read(*dataset).good())
  {
    DRTROIContourSequence &rtROIContourSequenceObject = rtStructureSetObject.getROIContourSequence();
    if (rtROIContourSequenceObject.gotoFirstItem().good())
    {
      do // For all ROIs
      {
        DRTROIContourSequence::Item &currentRoiObject = rtROIContourSequenceObject.getCurrentItem();
        if (currentRoiObject.isValid())
        {
          DRTContourSequence &rtContourSequenceObject = currentRoiObject.getContourSequence();
          if (rtContourSequenceObject.gotoFirstItem().good())
          {
            do // For all contours
            {
              DRTContourSequence::Item &contourItem = rtContourSequenceObject.getCurrentItem();
              if (!contourItem.isValid())
              {
                DRTContourImageSequence &rtContourImageSequenceObject = contourItem.getContourImageSequence();
                if (rtContourImageSequenceObject.gotoFirstItem().good())
                {
                  DRTContourImageSequence::Item &rtContourImageSequenceItem = rtContourImageSequenceObject.getCurrentItem();
                  if (rtContourImageSequenceItem.isValid())
                  {
                    OFString referencedSOPInstanceUID("");
                    if (rtContourImageSequenceItem.getReferencedSOPInstanceUID(referencedSOPInstanceUID).good())
                    {
                      referencedSOPInstanceUIDs.push_back(referencedSOPInstanceUID);
                    }
                  }
                }
              }
            } // For all contours
            while (rtContourSequenceObject.gotoNextItem().good());
          }
        }
      } // For all ROIs
      while (rtROIContourSequenceObject.gotoNextItem().good());
    } // End ROIContourSequence

    // If the above tags do not store the referenced instance UIDs, then look at the other possible place
    if (referencedSOPInstanceUIDs.empty())
    {
      DRTReferencedFrameOfReferenceSequence &rtReferencedFrameOfReferenceSequenceObject = rtStructureSetObject.getReferencedFrameOfReferenceSequence();
      if (rtReferencedFrameOfReferenceSequenceObject.gotoFirstItem().good())
      {
        DRTReferencedFrameOfReferenceSequence::Item &currentReferencedFrameOfReferenceSequenceItem = rtReferencedFrameOfReferenceSequenceObject.getCurrentItem();
        if (currentReferencedFrameOfReferenceSequenceItem.isValid())
        {
          DRTRTReferencedStudySequence &rtReferencedStudySequenceObject = currentReferencedFrameOfReferenceSequenceItem.getRTReferencedStudySequence();
          if (rtReferencedStudySequenceObject.gotoFirstItem().good())
          {
            DRTRTReferencedStudySequence::Item &rtReferencedStudySequenceItem = rtReferencedStudySequenceObject.getCurrentItem();
            if (rtReferencedStudySequenceItem.isValid())
            {
              DRTRTReferencedSeriesSequence &rtReferencedSeriesSequenceObject = rtReferencedStudySequenceItem.getRTReferencedSeriesSequence();
              if (rtReferencedSeriesSequenceObject.gotoFirstItem().good())
              {
                if (rtReferencedSeriesSequenceObject.gotoFirstItem().good())
                {
                  DRTRTReferencedSeriesSequence::Item &rtReferencedSeriesSequenceItem = rtReferencedSeriesSequenceObject.getCurrentItem();
                  if (rtReferencedSeriesSequenceItem.isValid())
                  {
                    DRTContourImageSequence &rtContourImageSequenceObject = rtReferencedSeriesSequenceItem.getContourImageSequence();
                    if (rtContourImageSequenceObject.gotoFirstItem().good())
                    {
                      do
                      {
                        DRTContourImageSequence::Item &rtContourImageSequenceItem = rtContourImageSequenceObject.getCurrentItem();
                        if (rtContourImageSequenceItem.isValid())
                        {
                          OFString referencedSOPInstanceUID("");
                          if (rtContourImageSequenceItem.getReferencedSOPInstanceUID(referencedSOPInstanceUID).good())
                          {
                            referencedSOPInstanceUIDs.push_back(referencedSOPInstanceUID);
                          }
                        }
                      } // For all contours
                      while (rtContourImageSequenceObject.gotoNextItem().good());
                    }
                  }
                }
              }
            }
          }
        }
      }
    } // End DRTReferencedFrameOfReferenceSequence
  } // End finding referenced instance UIDs
}

//-----------------------------------------------------------------------------
void vtkSlicerDicomRtImportExportModuleLogic::vtkInternal::ExamineRtImageDataset(DcmDataset* dataset, OFString &name, std::vector<OFString> &referencedSOPInstanceUIDs)
{
  if (!dataset)
    {
    return;
    }

  // Assemble name
  name += "RTIMAGE";
  OFString imageLabel;
  dataset->findAndGetOFString(DCM_RTImageLabel, imageLabel);
  if (!imageLabel.empty())
  {
    name += ": " + imageLabel;
  }

  // Get referenced RTPlan
  OFString referencedSOPInstanceUID("");
  DRTImageIOD rtImageObject;
  if (rtImageObject.read(*dataset).good())
  {
    DRTReferencedRTPlanSequenceInRTImageModule &rtReferencedRtPlanSequenceObject = rtImageObject.getReferencedRTPlanSequence();
    if (rtReferencedRtPlanSequenceObject.gotoFirstItem().good())
    {
      DRTReferencedRTPlanSequenceInRTImageModule::Item &currentReferencedRtPlanSequenceObject = rtReferencedRtPlanSequenceObject.getCurrentItem();
      if (currentReferencedRtPlanSequenceObject.getReferencedSOPInstanceUID(referencedSOPInstanceUID).good())
      {
        referencedSOPInstanceUIDs.push_back(referencedSOPInstanceUID);
      }
    }
  }
}

//---------------------------------------------------------------------------
bool vtkSlicerDicomRtImportExportModuleLogic::vtkInternal::LoadRtDose(vtkSlicerDicomRtReader* rtReader, vtkSlicerDICOMLoadable* loadable)
{
  vtkMRMLScene* scene = this->External->GetMRMLScene();
  if (!scene)
  {
    vtkErrorWithObjectMacro(this->External, "LoadRtDose: Invalid MRML scene");
    return false;
  }

  const char* fileName = loadable->GetFiles()->GetValue(0);
  const char* seriesName = loadable->GetName();

  // Load Volume
  vtkSmartPointer<vtkMRMLVolumeArchetypeStorageNode> volumeStorageNode = vtkSmartPointer<vtkMRMLVolumeArchetypeStorageNode>::New();
  vtkSmartPointer<vtkMRMLScalarVolumeNode> volumeNode = vtkSmartPointer<vtkMRMLScalarVolumeNode>::New();
  volumeStorageNode->SetFileName(fileName);
  volumeStorageNode->ResetFileNameList();
  volumeStorageNode->SetSingleFile(1);

  // Read volume from disk
  if (!volumeStorageNode->ReadData(volumeNode))
  {
    vtkErrorWithObjectMacro(this->External, "LoadRtDose: Failed to load dose volume file '" << fileName << "' (series name '" << seriesName << "')");
    return false;
  }

  volumeNode->SetScene(this->External->GetMRMLScene());
  std::string volumeNodeName = scene->GenerateUniqueName(seriesName);
  volumeNode->SetName(volumeNodeName.c_str());

  // Set new spacing
  double* initialSpacing = volumeNode->GetSpacing();
  double* correctSpacing = rtReader->GetPixelSpacing();
  volumeNode->SetSpacing(correctSpacing[0], correctSpacing[1], initialSpacing[2]);
  volumeNode->SetAttribute(SlicerRtCommon::DICOMRTIMPORT_DOSE_VOLUME_IDENTIFIER_ATTRIBUTE_NAME.c_str(), "1");
  scene->AddNode(volumeNode);

  // Apply dose grid scaling
  if (!rtReader->GetDoseGridScaling())
  {
    vtkErrorWithObjectMacro(this->External, "LoadRtDose: Empty dose unit value found for dose volume " << volumeNode->GetName());
  }
  double doseGridScaling = vtkVariant(rtReader->GetDoseGridScaling()).ToDouble();

  vtkSmartPointer<vtkImageData> floatVolumeData = vtkSmartPointer<vtkImageData>::New();

  vtkSmartPointer<vtkImageCast> imageCast = vtkSmartPointer<vtkImageCast>::New();
  imageCast->SetInputData(volumeNode->GetImageData());
  imageCast->SetOutputScalarTypeToFloat();
  imageCast->Update();
  floatVolumeData->DeepCopy(imageCast->GetOutput());

  float value = 0.0;
  float* floatPtr = (float*)floatVolumeData->GetScalarPointer();
  for (long i=0; i<floatVolumeData->GetNumberOfPoints(); ++i)
  {
    value = (*floatPtr) * doseGridScaling;
    (*floatPtr) = value;
    ++floatPtr;
  }

  volumeNode->SetAndObserveImageData(floatVolumeData);

  // Get default isodose color table and default dose color table
  vtkMRMLColorTableNode* defaultIsodoseColorTable = vtkSlicerIsodoseModuleLogic::CreateDefaultIsodoseColorTable(scene);
  vtkMRMLColorTableNode* defaultDoseColorTable = vtkSlicerIsodoseModuleLogic::CreateDefaultDoseColorTable(scene);
  if (!defaultIsodoseColorTable || !defaultDoseColorTable)
  {
    vtkErrorWithObjectMacro(this->External, "LoadRtDose: Failed to get default color tables");
    return false;
  }

  //TODO: Generate isodose surfaces if chosen so by the user in the hanging protocol options (hanging protocol support not implemented yet)

  // Set default colormap to the loaded one if found or generated, or to rainbow otherwise
  vtkSmartPointer<vtkMRMLScalarVolumeDisplayNode> volumeDisplayNode = vtkSmartPointer<vtkMRMLScalarVolumeDisplayNode>::New();
  volumeDisplayNode->SetAndObserveColorNodeID(defaultDoseColorTable->GetID());
  scene->AddNode(volumeDisplayNode);
  volumeNode->SetAndObserveDisplayNodeID(volumeDisplayNode->GetID());

  // Set window/level to match the isodose levels
  int minDoseInDefaultIsodoseLevels = vtkVariant(defaultIsodoseColorTable->GetColorName(0)).ToInt();
  int maxDoseInDefaultIsodoseLevels = vtkVariant(defaultIsodoseColorTable->GetColorName(defaultIsodoseColorTable->GetNumberOfColors()-1)).ToInt();

  volumeDisplayNode->AutoWindowLevelOff();
  volumeDisplayNode->SetWindowLevelMinMax(minDoseInDefaultIsodoseLevels, maxDoseInDefaultIsodoseLevels);

  // Set display threshold
  volumeDisplayNode->AutoThresholdOff();
  volumeDisplayNode->SetLowerThreshold(0.5 * doseGridScaling);
  volumeDisplayNode->SetApplyThreshold(1);

  // Setup subject hierarchy entry
  vtkMRMLSubjectHierarchyNode* shNode = vtkMRMLSubjectHierarchyNode::GetSubjectHierarchyNode(this->External->GetMRMLScene());
  if (!shNode)
  {
    vtkErrorWithObjectMacro(this->External, "LoadRtDose: Failed to access subject hierarchy node");
    return false;
  }
  vtkIdType seriesItemID = shNode->CreateItem(shNode->GetSceneItemID(), volumeNode);
  if (rtReader->GetSeriesInstanceUid())
  {
    shNode->SetItemUID(seriesItemID, vtkMRMLSubjectHierarchyConstants::GetDICOMUIDName(), rtReader->GetSeriesInstanceUid());
  }
  else
  {
    vtkErrorWithObjectMacro(this->External, "LoadRtDose: series instance UID not found for dose volume " << volumeNode->GetName());
  }
  if (rtReader->GetRTDoseReferencedRTPlanSOPInstanceUID())
  {
    shNode->SetItemAttribute(seriesItemID, vtkMRMLSubjectHierarchyConstants::GetDICOMReferencedInstanceUIDsAttributeName(),
      rtReader->GetRTDoseReferencedRTPlanSOPInstanceUID());
  }
  else
  {
    vtkErrorWithObjectMacro(this->External, "LoadRtDose: RTDoseReferencedRTPlanSOPInstanceUID not found for dose volume " << volumeNode->GetName());
  }

  // Insert series in subject hierarchy
  this->InsertSeriesInSubjectHierarchy(rtReader);

  // Set dose unit attributes to subject hierarchy study item
  vtkIdType studyItemID = shNode->GetItemParent(seriesItemID);
  if (studyItemID != vtkMRMLSubjectHierarchyNode::INVALID_ITEM_ID)
  {
    std::string existingDoseUnitName = shNode->GetItemAttribute(studyItemID, SlicerRtCommon::DICOMRTIMPORT_DOSE_UNIT_NAME_ATTRIBUTE_NAME);
    if (!rtReader->GetDoseUnits())
    {
      vtkErrorWithObjectMacro(this->External, "LoadRtDose: Empty dose unit name found for dose volume " << volumeNode->GetName());
    }
    else if (!existingDoseUnitName.empty() && existingDoseUnitName.compare(rtReader->GetDoseUnits()))
    {
      vtkErrorWithObjectMacro(this->External, "LoadRtDose: Dose unit name already exists (" << existingDoseUnitName << ") for study and differs from current one (" << rtReader->GetDoseUnits() << ")");
    }
    else
    {
      shNode->SetItemAttribute(studyItemID, SlicerRtCommon::DICOMRTIMPORT_DOSE_UNIT_NAME_ATTRIBUTE_NAME, rtReader->GetDoseUnits());
    }

    std::string existingDoseUnitValueStr = shNode->GetItemAttribute(studyItemID, SlicerRtCommon::DICOMRTIMPORT_DOSE_UNIT_VALUE_ATTRIBUTE_NAME);
    if (!rtReader->GetDoseGridScaling())
    {
      vtkErrorWithObjectMacro(this->External, "LoadRtDose: Empty dose unit value found for dose volume " << volumeNode->GetName());
    }
    else if (!existingDoseUnitValueStr.empty())
    {
      double existingDoseUnitValue = vtkVariant(existingDoseUnitValueStr).ToDouble();
      double doseGridScaling = vtkVariant(rtReader->GetDoseGridScaling()).ToDouble();
      double currentDoseUnitValue = vtkVariant(rtReader->GetDoseGridScaling()).ToDouble();
      if (fabs(existingDoseUnitValue - currentDoseUnitValue) > EPSILON)
      {
        vtkErrorWithObjectMacro(this->External, "LoadRtDose: Dose unit value already exists (" << existingDoseUnitValue << ") for study and differs from current one (" << currentDoseUnitValue << ")");
      }
    }
    else
    {
      shNode->SetItemAttribute(studyItemID, SlicerRtCommon::DICOMRTIMPORT_DOSE_UNIT_VALUE_ATTRIBUTE_NAME, rtReader->GetDoseGridScaling());
    }
  }
  else
  {
    vtkErrorWithObjectMacro(this->External, "LoadRtDose: Unable to get parent study hierarchy node for dose volume '" << volumeNode->GetName() << "'");
  }

  // Select as active volume
  if (this->External->GetApplicationLogic()!=NULL)
  {
    if (this->External->GetApplicationLogic()->GetSelectionNode()!=NULL)
    {
      this->External->GetApplicationLogic()->GetSelectionNode()->SetReferenceActiveVolumeID(volumeNode->GetID());
      this->External->GetApplicationLogic()->PropagateVolumeSelection();
    }
  }
  return true;
}

//---------------------------------------------------------------------------
bool vtkSlicerDicomRtImportExportModuleLogic::vtkInternal::LoadRtPlan(vtkSlicerDicomRtReader* rtReader, vtkSlicerDICOMLoadable* loadable)
{
  vtkMRMLScene* scene = this->External->GetMRMLScene();
  if (!scene)
  {
    vtkErrorWithObjectMacro(this->External, "LoadRtPlan: Invalid MRML scene");
    return false;
  }
  vtkMRMLSubjectHierarchyNode* shNode = vtkMRMLSubjectHierarchyNode::GetSubjectHierarchyNode(this->External->GetMRMLScene());
  if (!shNode)
  {
    vtkErrorWithObjectMacro(this->External, "LoadRtPlan: Failed to access subject hierarchy node");
    return false;
  }

  vtkSmartPointer<vtkMRMLModelHierarchyNode> beamModelHierarchyRootNode;

  const char* seriesName = loadable->GetName();

  scene->StartState(vtkMRMLScene::BatchProcessState);

  // Create plan node
  vtkSmartPointer<vtkMRMLRTPlanNode> planNode = vtkSmartPointer<vtkMRMLRTPlanNode>::New();
  planNode->SetName(seriesName);
  scene->AddNode(planNode);

  // Set up plan subject hierarchy node
  vtkIdType planShItemID = planNode->GetPlanSubjectHierarchyItemID();
  if (planShItemID == vtkMRMLSubjectHierarchyNode::INVALID_ITEM_ID)
  {
    vtkErrorWithObjectMacro(this->External, "LoadRtPlan: Created RT plan node, but it doesn't have a subject hierarchy item");
    return false;
  }

  // Attach attributes to plan subject hierarchy item
  shNode->SetItemUID(planShItemID, vtkMRMLSubjectHierarchyConstants::GetDICOMUIDName(), rtReader->GetSeriesInstanceUid());
  shNode->SetItemName(planShItemID, seriesName);

  const char* referencedStructureSetSopInstanceUid = rtReader->GetRTPlanReferencedStructureSetSOPInstanceUID();
  const char* referencedDoseSopInstanceUids = rtReader->GetRTPlanReferencedDoseSOPInstanceUIDs();
  std::string referencedSopInstanceUids = "";
  if (referencedStructureSetSopInstanceUid)
  {
    referencedSopInstanceUids = std::string(referencedStructureSetSopInstanceUid);
  }
  if (referencedDoseSopInstanceUids)
  {
    referencedSopInstanceUids = referencedSopInstanceUids +
      (referencedStructureSetSopInstanceUid?" ":"") + std::string(referencedDoseSopInstanceUids);
  }
  shNode->SetItemAttribute(planShItemID,
    vtkMRMLSubjectHierarchyConstants::GetDICOMReferencedInstanceUIDsAttributeName(), referencedSopInstanceUids );

  // Load beams in plan
  int numberOfBeams = rtReader->GetNumberOfBeams();
  for (int beamIndex = 0; beamIndex < numberOfBeams; beamIndex++) // DICOM starts indexing from 1
  {
    unsigned int dicomBeamNumber = rtReader->GetBeamNumberForIndex(beamIndex);
    const char* beamName = rtReader->GetBeamName(dicomBeamNumber);

    // Create the beam node
    vtkSmartPointer<vtkMRMLRTBeamNode> beamNode = vtkSmartPointer<vtkMRMLRTBeamNode>::New();
    beamNode->SetName(beamName);

    // Set beam geometry parameters from DICOM
    double jawPositions[2][2] = {{0.0, 0.0},{0.0, 0.0}};
    rtReader->GetBeamLeafJawPositions(dicomBeamNumber, jawPositions);
    beamNode->SetX1Jaw(jawPositions[0][0]);
    beamNode->SetX2Jaw(jawPositions[0][1]);
    beamNode->SetY1Jaw(jawPositions[1][0]);
    beamNode->SetY2Jaw(jawPositions[1][1]);

    beamNode->SetGantryAngle(rtReader->GetBeamGantryAngle(dicomBeamNumber));
    beamNode->SetCollimatorAngle(rtReader->GetBeamBeamLimitingDeviceAngle(dicomBeamNumber));
    beamNode->SetCouchAngle(rtReader->GetBeamPatientSupportAngle(dicomBeamNumber));

    beamNode->SetSAD(rtReader->GetBeamSourceAxisDistance(dicomBeamNumber));

    // Set isocenter to parent plan
    double* isocenter = rtReader->GetBeamIsocenterPositionRas(dicomBeamNumber);
    planNode->SetIsocenterSpecification(vtkMRMLRTPlanNode::ArbitraryPoint);
    if (beamIndex == 0)
    {
      if (!planNode->SetIsocenterPosition(isocenter))
      {
        vtkErrorWithObjectMacro(this->External, "LoadRtPlan: Failed to set isocenter position");
        return false;
      }
    }
    else
    {
      double planIsocenter[3] = {0.0, 0.0, 0.0};
      if (!planNode->GetIsocenterPosition(planIsocenter))
      {
        vtkErrorWithObjectMacro(this->External, "LoadRtPlan: Failed to get plan isocenter position");
        return false;
      }
      //TODO: Multiple isocenters per plan is not yet supported. Will be part of the beams group nodes developed later
      if ( !SlicerRtCommon::AreEqualWithTolerance(planIsocenter[0], isocenter[0])
        || !SlicerRtCommon::AreEqualWithTolerance(planIsocenter[1], isocenter[1])
        || !SlicerRtCommon::AreEqualWithTolerance(planIsocenter[2], isocenter[2]) )
      {
        vtkErrorWithObjectMacro(this->External, "LoadRtPlan: Different isocenters for each beam are not yet supported! The first isocenter will be used for the whole plan " << planNode->GetName() << ": (" << planIsocenter[0] << ", " << planIsocenter[1] << ", " << planIsocenter[2] << ")");
      }
    }

    // Add beam to scene (triggers poly data and transform creation and update)
    scene->AddNode(beamNode);
    // Add beam to plan
    planNode->AddBeam(beamNode);
    // Update beam transforms (batch processing prevents processing events that would do this)
    this->External->BeamsLogic->UpdateTransformForBeam(beamNode);

    // Create beam model hierarchy root node if has not been created yet
    if (beamModelHierarchyRootNode.GetPointer() == NULL)
    {
      beamModelHierarchyRootNode = vtkSmartPointer<vtkMRMLModelHierarchyNode>::New();
      std::string beamModelHierarchyRootNodeName = seriesName + SlicerRtCommon::DICOMRTIMPORT_BEAMMODEL_HIERARCHY_NODE_NAME_POSTFIX;
      beamModelHierarchyRootNodeName = scene->GenerateUniqueName(beamModelHierarchyRootNodeName);
      beamModelHierarchyRootNode->SetName(beamModelHierarchyRootNodeName.c_str());
      beamModelHierarchyRootNode->SetAttribute(vtkMRMLSubjectHierarchyConstants::GetSubjectHierarchyExcludeFromTreeAttributeName().c_str(), "1");
      scene->AddNode(beamModelHierarchyRootNode);

      // Create display node for the hierarchy node
      vtkSmartPointer<vtkMRMLModelDisplayNode> beamModelHierarchyRootDisplayNode = vtkSmartPointer<vtkMRMLModelDisplayNode>::New();
      std::string beamModelHierarchyRootDisplayNodeName = beamModelHierarchyRootNodeName + std::string("Display");
      beamModelHierarchyRootDisplayNode->SetName(beamModelHierarchyRootDisplayNodeName.c_str());
      beamModelHierarchyRootDisplayNode->SetVisibility(1);
      scene->AddNode(beamModelHierarchyRootDisplayNode);
      beamModelHierarchyRootNode->SetAndObserveDisplayNodeID( beamModelHierarchyRootDisplayNode->GetID() );
    }

    // Put beam model in the model hierarchy
    vtkSmartPointer<vtkMRMLModelHierarchyNode> beamModelHierarchyNode = vtkSmartPointer<vtkMRMLModelHierarchyNode>::New();
    std::string beamModelHierarchyNodeName = std::string(beamNode->GetName()) + SlicerRtCommon::DICOMRTIMPORT_MODEL_HIERARCHY_NODE_NAME_POSTFIX;
    beamModelHierarchyNode->SetName(beamModelHierarchyNodeName.c_str());
    scene->AddNode(beamModelHierarchyNode);
    beamModelHierarchyNode->SetAssociatedNodeID(beamNode->GetID());
    beamModelHierarchyNode->SetParentNodeID(beamModelHierarchyRootNode->GetID());
    beamModelHierarchyNode->SetIndexInParent(beamIndex);
    beamModelHierarchyNode->HideFromEditorsOn();

    // Create display node for the hierarchy node
    vtkSmartPointer<vtkMRMLModelDisplayNode> beamModelHierarchyDisplayNode = vtkSmartPointer<vtkMRMLModelDisplayNode>::New();
    std::string beamModelHierarchyDisplayNodeName = beamModelHierarchyNodeName + std::string("Display");
    beamModelHierarchyDisplayNode->SetName(beamModelHierarchyDisplayNodeName.c_str());
    beamModelHierarchyDisplayNode->SetVisibility(1);
    scene->AddNode(beamModelHierarchyDisplayNode);
    beamModelHierarchyNode->SetAndObserveDisplayNodeID( beamModelHierarchyDisplayNode->GetID() );
  }

  // Insert plan isocenter series in subject hierarchy
  this->InsertSeriesInSubjectHierarchy(rtReader);

  // Put plan SH item underneath study
  vtkIdType studyItemID = shNode->GetItemByUID(
    vtkMRMLSubjectHierarchyConstants::GetDICOMUIDName(), rtReader->GetStudyInstanceUid());
  if (studyItemID == vtkMRMLSubjectHierarchyNode::INVALID_ITEM_ID)
  {
    vtkErrorWithObjectMacro(this->External, "LoadRtPlan: Failed to find study subject hierarchy item");
    return false;
  }
  if (planShItemID != vtkMRMLSubjectHierarchyNode::INVALID_ITEM_ID)
  {
    shNode->SetItemParent(planShItemID, studyItemID);
  }
  // Put plan markups under study within SH
  vtkIdType planMarkupsShItemID = shNode->GetItemByDataNode(planNode->GetPoisMarkupsFiducialNode());
  if (planMarkupsShItemID != vtkMRMLSubjectHierarchyNode::INVALID_ITEM_ID)
  {
    shNode->SetItemParent(planMarkupsShItemID, studyItemID);
  }

  // Compute and set geometry of possible RT image that references the loaded beams.
  // Uses the referenced RT image if available, otherwise the geometry will be set up when loading the corresponding RT image
  vtkSmartPointer<vtkCollection> beams = vtkSmartPointer<vtkCollection>::New();
  planNode->GetBeams(beams);
  if (beams)
  {
    for (int i=0; i<beams->GetNumberOfItems(); ++i)
    {
      vtkMRMLRTBeamNode *beamNode = vtkMRMLRTBeamNode::SafeDownCast(beams->GetItemAsObject(i));
      this->SetupRtImageGeometry(beamNode);
    }
  }

  scene->EndState(vtkMRMLScene::BatchProcessState);

  return true;
}

//---------------------------------------------------------------------------
bool vtkSlicerDicomRtImportExportModuleLogic::vtkInternal::LoadRtStructureSet(vtkSlicerDicomRtReader* rtReader, vtkSlicerDICOMLoadable* loadable)
{
  vtkMRMLScene* scene = this->External->GetMRMLScene();
  if (!scene)
  {
    vtkErrorWithObjectMacro(this->External, "LoadRtStructureSet: Invalid MRML scene");
    return false;
  }
  vtkMRMLSubjectHierarchyNode* shNode = vtkMRMLSubjectHierarchyNode::GetSubjectHierarchyNode(this->External->GetMRMLScene());
  if (!shNode)
  {
    vtkErrorWithObjectMacro(this->External, "LoadRtStructureSet: Failed to access subject hierarchy node");
    return false;
  }

  vtkIdType fiducialSeriesShItemID = vtkMRMLSubjectHierarchyNode::INVALID_ITEM_ID;
  vtkIdType segmentationShItemID = vtkMRMLSubjectHierarchyNode::INVALID_ITEM_ID;
  vtkSmartPointer<vtkMRMLSegmentationNode> segmentationNode;
  vtkSmartPointer<vtkMRMLSegmentationDisplayNode> segmentationDisplayNode;

  const char* fileName = loadable->GetFiles()->GetValue(0);
  const char* seriesName = loadable->GetName();
  std::string structureSetReferencedSeriesUid("");

  scene->StartState(vtkMRMLScene::BatchProcessState);

  // Get referenced SOP instance UIDs
  const char* referencedSopInstanceUids = rtReader->GetRTStructureSetReferencedSOPInstanceUIDs();
  // Number of loaded points. Used to prevent unreasonably long loading times with the downside of a less nice initial representation
  long maximumNumberOfPoints = -1;
  long totalNumberOfPoints = 0;

  // Add ROIs
  int numberOfRois = rtReader->GetNumberOfRois();
  for (int internalROIIndex=0; internalROIIndex<numberOfRois; internalROIIndex++)
  {
    // Get name and color
    const char* roiLabel = rtReader->GetRoiName(internalROIIndex);
    double *roiColor = rtReader->GetRoiDisplayColor(internalROIIndex);

    // Get structure
    vtkPolyData* roiPolyData = rtReader->GetRoiPolyData(internalROIIndex);
    if (roiPolyData == NULL)
    {
      vtkWarningWithObjectMacro(this->External, "LoadRtStructureSet: Invalid structure ROI data for ROI named '"
        << (roiLabel?roiLabel:"Unnamed") << "' in file '" << fileName
        << "' (internal ROI index: " << internalROIIndex << ")");
      continue;
    }
    if (roiPolyData->GetNumberOfPoints() == 0)
    {
      vtkWarningWithObjectMacro(this->External, "LoadRtStructureSet: Structure ROI data does not contain any points for ROI named '"
        << (roiLabel?roiLabel:"Unnamed") << "' in file '" << fileName
        << "' (internal ROI index: " << internalROIIndex << ")");
      continue;
    }
    if (maximumNumberOfPoints < roiPolyData->GetNumberOfPoints())
    {
      maximumNumberOfPoints = roiPolyData->GetNumberOfPoints();
    }
    totalNumberOfPoints += roiPolyData->GetNumberOfPoints();

    // Get referenced series UID
    const char* roiReferencedSeriesUid = rtReader->GetRoiReferencedSeriesUid(internalROIIndex);
    if (structureSetReferencedSeriesUid.empty())
    {
      structureSetReferencedSeriesUid = std::string(roiReferencedSeriesUid);
    }
    else if (roiReferencedSeriesUid && STRCASECMP(structureSetReferencedSeriesUid.c_str(), roiReferencedSeriesUid))
    {
      vtkWarningWithObjectMacro(this->External, "LoadRtStructureSet: ROIs in structure set '" << seriesName << "' have different referenced series UIDs");
    }

    //
    // Point ROI (fiducial)
    //
    if (roiPolyData->GetNumberOfPoints() == 1)
    {
      // Set up subject hierarchy item for the series, if it has not been done yet.
      // Only create it for fiducials, as all structures are stored in a single segmentation node
      if (fiducialSeriesShItemID == vtkMRMLSubjectHierarchyNode::INVALID_ITEM_ID)
      {
        std::string fiducialsSeriesName(seriesName);
        fiducialsSeriesName.append(SlicerRtCommon::DICOMRTIMPORT_FIDUCIALS_HIERARCHY_NODE_NAME_POSTFIX);
        fiducialsSeriesName = scene->GenerateUniqueName(fiducialsSeriesName);
        fiducialSeriesShItemID = shNode->CreateFolderItem(shNode->GetSceneItemID(), fiducialsSeriesName);
        shNode->SetItemUID(fiducialSeriesShItemID,
          vtkMRMLSubjectHierarchyConstants::GetDICOMUIDName(), rtReader->GetSeriesInstanceUid() );
      }

      // Creates fiducial MRML node and display node
      vtkMRMLDisplayableNode* fiducialNode = this->AddRoiPoint(roiPolyData->GetPoint(0), roiLabel, roiColor);

      // Setup subject hierarchy entry for the ROI
      vtkIdType fiducialShItemID = shNode->CreateItem(fiducialSeriesShItemID, fiducialNode);
      shNode->SetItemAttribute(fiducialShItemID, SlicerRtCommon::DICOMRTIMPORT_ROI_REFERENCED_SERIES_UID_ATTRIBUTE_NAME, roiReferencedSeriesUid);
    }
    //
    // Contour ROI (segmentation)
    //
    else
    {
      // Create segmentation node for the structure set series, if not created yet
      if (segmentationNode.GetPointer() == NULL)
      {
        segmentationNode = vtkSmartPointer<vtkMRMLSegmentationNode>::New();
        std::string segmentationNodeName = scene->GenerateUniqueName(seriesName);
        segmentationNode->SetName(segmentationNodeName.c_str());
        scene->AddNode(segmentationNode);

        // Set master representation to planar contour
        segmentationNode->GetSegmentation()->SetMasterRepresentationName(vtkSegmentationConverter::GetSegmentationPlanarContourRepresentationName());

        // Get image geometry from previously loaded volume if found
        // Segmentation node checks added nodes and sets the geometry parameter in case the referenced volume is loaded later
        vtkIdType referencedVolumeShItemID = shNode->GetItemByUID(
          vtkMRMLSubjectHierarchyConstants::GetDICOMUIDName(), roiReferencedSeriesUid );
        if (referencedVolumeShItemID != vtkMRMLSubjectHierarchyNode::INVALID_ITEM_ID)
        {
          vtkMRMLScalarVolumeNode* referencedVolumeNode = vtkMRMLScalarVolumeNode::SafeDownCast(
            shNode->GetItemDataNode(referencedVolumeShItemID) );
          if (referencedVolumeNode)
          {
            segmentationNode->SetReferenceImageGeometryParameterFromVolumeNode(referencedVolumeNode);
          }
          else
          {
            vtkErrorWithObjectMacro(this->External, "LoadRtStructureSet: Referenced volume series item does not contain a volume");
          }
        }

        // Set up subject hierarchy node for segmentation
        segmentationShItemID = shNode->CreateItem(shNode->GetSceneItemID(), segmentationNode);
        shNode->SetItemUID(segmentationShItemID, vtkMRMLSubjectHierarchyConstants::GetDICOMUIDName(), rtReader->GetSeriesInstanceUid());
        shNode->SetItemAttribute(segmentationShItemID, SlicerRtCommon::DICOMRTIMPORT_ROI_REFERENCED_SERIES_UID_ATTRIBUTE_NAME, structureSetReferencedSeriesUid);
        shNode->SetItemAttribute(segmentationShItemID,
          vtkMRMLSubjectHierarchyConstants::GetDICOMReferencedInstanceUIDsAttributeName(), referencedSopInstanceUids );

        // Setup segmentation display and storage
        segmentationDisplayNode = vtkSmartPointer<vtkMRMLSegmentationDisplayNode>::New();
        scene->AddNode(segmentationDisplayNode);
        segmentationNode->SetAndObserveDisplayNodeID(segmentationDisplayNode->GetID());
        segmentationDisplayNode->SetBackfaceCulling(0);
      }

      // Add segment for current structure
      vtkSmartPointer<vtkSegment> segment = vtkSmartPointer<vtkSegment>::New();
      segment->SetName(roiLabel);
      segment->SetColor(roiColor[0], roiColor[1], roiColor[2]);
      segment->AddRepresentation(vtkSegmentationConverter::GetSegmentationPlanarContourRepresentationName(), roiPolyData);
      segmentationNode->GetSegmentation()->AddSegment(segment);
    }
  } // for all ROIs

  // Force showing closed surface model instead of contour points and calculate auto opacity values for segments
  // Do not set closed surface display in case of extremely large structures, to prevent unreasonably long load times
  if (segmentationDisplayNode.GetPointer())
  {
    // Arbitrary thresholds, can revisit
    vtkDebugWithObjectMacro(this->External, "LoadRtStructureSet: Maximum number of points in a segment = " << maximumNumberOfPoints << ", Total number of points in segmentation = " << totalNumberOfPoints);
    if (maximumNumberOfPoints < 800000 && totalNumberOfPoints < 3000000)
    {
      segmentationDisplayNode->SetPreferredDisplayRepresentationName3D(vtkSegmentationConverter::GetSegmentationClosedSurfaceRepresentationName());
      segmentationDisplayNode->SetPreferredDisplayRepresentationName2D(vtkSegmentationConverter::GetSegmentationClosedSurfaceRepresentationName());
      segmentationDisplayNode->CalculateAutoOpacitiesForSegments();
    }
    else
    {
      vtkWarningWithObjectMacro(this->External, "LoadRtStructureSet: Structure set contains extremely large contours that will most likely take an unreasonably long time to load. No closed surface representation is thus created for nicer display, but the raw RICOM-RT planar contours are shown. It is possible to create nicer models in Segmentations module by converting to the lighter Ribbon model or the nicest Closed surface.");
    }
  }
  else if (segmentationNode.GetPointer())
  {
    vtkErrorWithObjectMacro(this->External, "LoadRtStructureSet: No display node was created for the segmentation node " << segmentationNode->GetName());
  }

  // Insert series in subject hierarchy
  this->InsertSeriesInSubjectHierarchy(rtReader);

  // Fire modified events if loading is finished
  scene->EndState(vtkMRMLScene::BatchProcessState);

  return true;
}

//---------------------------------------------------------------------------
bool vtkSlicerDicomRtImportExportModuleLogic::vtkInternal::LoadRtImage(vtkSlicerDicomRtReader* rtReader, vtkSlicerDICOMLoadable* loadable)
{
  vtkMRMLScene* scene = this->External->GetMRMLScene();
  if (!scene)
  {
    vtkErrorWithObjectMacro(this->External, "LoadRtImage: Invalid MRML scene");
    return false;
  }
  vtkMRMLSubjectHierarchyNode* shNode = vtkMRMLSubjectHierarchyNode::GetSubjectHierarchyNode(this->External->GetMRMLScene());
  if (!shNode)
  {
    vtkErrorWithObjectMacro(this->External, "LoadRtImage: Failed to access subject hierarchy node");
    return false;
  }

  const char* fileName = loadable->GetFiles()->GetValue(0);
  const char* seriesName = loadable->GetName();

  // Load Volume
  vtkSmartPointer<vtkMRMLVolumeArchetypeStorageNode> volumeStorageNode = vtkSmartPointer<vtkMRMLVolumeArchetypeStorageNode>::New();
  vtkSmartPointer<vtkMRMLScalarVolumeNode> volumeNode = vtkSmartPointer<vtkMRMLScalarVolumeNode>::New();
  volumeStorageNode->SetFileName(fileName);
  volumeStorageNode->ResetFileNameList();
  volumeStorageNode->SetSingleFile(1);

  // Read image from disk
  if (!volumeStorageNode->ReadData(volumeNode))
  {
    vtkErrorWithObjectMacro(this->External, "LoadRtImage: Failed to load RT image file '" << fileName << "' (series name '" << seriesName << "')");
    return false;
  }

  volumeNode->SetScene(scene);
  std::string volumeNodeName = scene->GenerateUniqueName(seriesName);
  volumeNode->SetName(volumeNodeName.c_str());
  scene->AddNode(volumeNode);

  // Create display node for the volume
  vtkSmartPointer<vtkMRMLScalarVolumeDisplayNode> volumeDisplayNode = vtkSmartPointer<vtkMRMLScalarVolumeDisplayNode>::New();
  scene->AddNode(volumeDisplayNode);
  volumeDisplayNode->SetDefaultColorMap();
  if (rtReader->GetWindowCenter() == 0.0 && rtReader->GetWindowWidth() == 0.0)
  {
    volumeDisplayNode->AutoWindowLevelOn();
  }
  else
  {
    // Apply given window level if available
    volumeDisplayNode->AutoWindowLevelOff();
    volumeDisplayNode->SetWindowLevel(rtReader->GetWindowWidth(), rtReader->GetWindowCenter());
  }
  volumeNode->SetAndObserveDisplayNodeID(volumeDisplayNode->GetID());

  // Set up subject hierarchy item
  vtkIdType seriesShItemID = shNode->CreateItem(shNode->GetSceneItemID(), volumeNode);
  shNode->SetItemUID(seriesShItemID, vtkMRMLSubjectHierarchyConstants::GetDICOMUIDName(), rtReader->GetSeriesInstanceUid());

  // Set RT image specific attributes
  shNode->SetItemAttribute(seriesShItemID, SlicerRtCommon::DICOMRTIMPORT_RTIMAGE_IDENTIFIER_ATTRIBUTE_NAME, "1");
  shNode->SetItemAttribute(seriesShItemID, vtkMRMLSubjectHierarchyConstants::GetDICOMReferencedInstanceUIDsAttributeName(),
    rtReader->GetRTImageReferencedRTPlanSOPInstanceUID());

  std::stringstream radiationMachineSadStream;
  radiationMachineSadStream << rtReader->GetRadiationMachineSAD();
  shNode->SetItemAttribute(seriesShItemID, SlicerRtCommon::DICOMRTIMPORT_SOURCE_AXIS_DISTANCE_ATTRIBUTE_NAME, radiationMachineSadStream.str());

  std::stringstream gantryAngleStream;
  gantryAngleStream << rtReader->GetGantryAngle();
  shNode->SetItemAttribute(seriesShItemID, SlicerRtCommon::DICOMRTIMPORT_GANTRY_ANGLE_ATTRIBUTE_NAME, gantryAngleStream.str());

  std::stringstream couchAngleStream;
  couchAngleStream << rtReader->GetPatientSupportAngle();
  shNode->SetItemAttribute(seriesShItemID, SlicerRtCommon::DICOMRTIMPORT_COUCH_ANGLE_ATTRIBUTE_NAME, couchAngleStream.str());

  std::stringstream collimatorAngleStream;
  collimatorAngleStream << rtReader->GetBeamLimitingDeviceAngle();
  shNode->SetItemAttribute(seriesShItemID, SlicerRtCommon::DICOMRTIMPORT_COLLIMATOR_ANGLE_ATTRIBUTE_NAME, collimatorAngleStream.str());

  std::stringstream referencedBeamNumberStream;
  referencedBeamNumberStream << rtReader->GetReferencedBeamNumber();
  shNode->SetItemAttribute(seriesShItemID, SlicerRtCommon::DICOMRTIMPORT_BEAM_NUMBER_ATTRIBUTE_NAME, referencedBeamNumberStream.str());

  std::stringstream rtImageSidStream;
  rtImageSidStream << rtReader->GetRTImageSID();
  shNode->SetItemAttribute(seriesShItemID, SlicerRtCommon::DICOMRTIMPORT_RTIMAGE_SID_ATTRIBUTE_NAME, rtImageSidStream.str());

  std::stringstream rtImagePositionStream;
  double rtImagePosition[2] = {0.0, 0.0};
  rtReader->GetRTImagePosition(rtImagePosition);
  rtImagePositionStream << rtImagePosition[0] << " " << rtImagePosition[1];
  shNode->SetItemAttribute(seriesShItemID,  SlicerRtCommon::DICOMRTIMPORT_RTIMAGE_POSITION_ATTRIBUTE_NAME, rtImagePositionStream.str());

  // Insert series in subject hierarchy
  this->InsertSeriesInSubjectHierarchy(rtReader);

  // Compute and set RT image geometry. Uses the referenced beam if available, otherwise the geometry will be set up when loading the referenced beam
  this->SetupRtImageGeometry(volumeNode);

  return true;
}

//---------------------------------------------------------------------------
vtkMRMLMarkupsFiducialNode* vtkSlicerDicomRtImportExportModuleLogic::vtkInternal::AddRoiPoint(double* roiPosition, std::string baseName, double* roiColor)
{
  std::string fiducialNodeName = this->External->GetMRMLScene()->GenerateUniqueName(baseName);
  vtkSmartPointer<vtkMRMLMarkupsFiducialNode> markupsNode = vtkSmartPointer<vtkMRMLMarkupsFiducialNode>::New();
  this->External->GetMRMLScene()->AddNode(markupsNode);
  markupsNode->SetName(baseName.c_str());
  markupsNode->AddFiducialFromArray(roiPosition);
  markupsNode->SetLocked(1);

  vtkSmartPointer<vtkMRMLMarkupsDisplayNode> markupsDisplayNode = vtkSmartPointer<vtkMRMLMarkupsDisplayNode>::New();
  this->External->GetMRMLScene()->AddNode(markupsDisplayNode);
  markupsDisplayNode->SetGlyphType(vtkMRMLMarkupsDisplayNode::Sphere3D);
  markupsDisplayNode->SetColor(roiColor);
  markupsNode->SetAndObserveDisplayNodeID(markupsDisplayNode->GetID());

  // Hide the fiducial by default
  markupsNode->SetDisplayVisibility(0);

  return markupsNode;
}

//---------------------------------------------------------------------------
void vtkSlicerDicomRtImportExportModuleLogic::vtkInternal::InsertSeriesInSubjectHierarchy(vtkSlicerDicomRtReader* rtReader)
{
  // Get the higher level parent items by their IDs (to fill their attributes later if they do not exist yet)
  vtkMRMLSubjectHierarchyNode* shNode = vtkMRMLSubjectHierarchyNode::GetSubjectHierarchyNode(this->External->GetMRMLScene());
  if (!shNode)
  {
    vtkErrorWithObjectMacro(this->External, "InsertSeriesInSubjectHierarchy: Failed to access subject hierarchy node");
    return;
  }

  vtkIdType patientItemID = shNode->GetItemByUID(vtkMRMLSubjectHierarchyConstants::GetDICOMUIDName(), rtReader->GetPatientId());
  vtkIdType studyItemID = shNode->GetItemByUID(vtkMRMLSubjectHierarchyConstants::GetDICOMUIDName(), rtReader->GetStudyInstanceUid());

  // Insert series in hierarchy
  vtkIdType seriesItemID = vtkSlicerSubjectHierarchyModuleLogic::InsertDicomSeriesInHierarchy(
    shNode, rtReader->GetPatientId(), rtReader->GetStudyInstanceUid(), rtReader->GetSeriesInstanceUid() );

  // Fill patient and study attributes if they have been just created
  if (patientItemID == vtkMRMLSubjectHierarchyNode::INVALID_ITEM_ID)
  {
    patientItemID = shNode->GetItemByUID(vtkMRMLSubjectHierarchyConstants::GetDICOMUIDName(), rtReader->GetPatientId());
    if (patientItemID != vtkMRMLSubjectHierarchyNode::INVALID_ITEM_ID)
    {
      // Add attributes for DICOM tags
      shNode->SetItemAttribute(patientItemID,
        vtkMRMLSubjectHierarchyConstants::GetDICOMPatientNameAttributeName(), rtReader->GetPatientName() );
      shNode->SetItemAttribute(patientItemID,
        vtkMRMLSubjectHierarchyConstants::GetDICOMPatientIDAttributeName(), rtReader->GetPatientId() );
      shNode->SetItemAttribute(patientItemID,
        vtkMRMLSubjectHierarchyConstants::GetDICOMPatientSexAttributeName(), rtReader->GetPatientSex() );
      shNode->SetItemAttribute(patientItemID,
        vtkMRMLSubjectHierarchyConstants::GetDICOMPatientBirthDateAttributeName(), rtReader->GetPatientBirthDate() );
      shNode->SetItemAttribute(patientItemID,
        vtkMRMLSubjectHierarchyConstants::GetDICOMPatientCommentsAttributeName(), rtReader->GetPatientComments() );

      // Set item name
      std::string patientItemName = ( !SlicerRtCommon::IsStringNullOrEmpty(rtReader->GetPatientName())
        ? std::string(rtReader->GetPatientName()) : SlicerRtCommon::DICOMRTIMPORT_NO_NAME );
      QSettings* settings = qSlicerApplication::application()->settingsDialog()->settings();
      bool displayPatientID = settings->value("SubjectHierarchy/DisplayPatientIDInSubjectHierarchyItemName").toBool();
      if ( displayPatientID && !SlicerRtCommon::IsStringNullOrEmpty(rtReader->GetPatientId()) )
      {
        patientItemName += " (" + std::string(rtReader->GetPatientId()) + ")";
      }
      bool displayPatientBirthDate = settings->value("SubjectHierarchy/DisplayPatientBirthDateInSubjectHierarchyItemName").toBool();
      if ( displayPatientBirthDate && !SlicerRtCommon::IsStringNullOrEmpty(rtReader->GetPatientBirthDate()) )
      {
        patientItemName += " (" + std::string(rtReader->GetPatientBirthDate()) + ")";
      }
      shNode->SetItemName(patientItemID, patientItemName);
    }
    else
    {
      vtkErrorWithObjectMacro(this->External, "InsertSeriesInSubjectHierarchy: Patient item has not been created for series with Instance UID "
        << (rtReader->GetSeriesInstanceUid() ? rtReader->GetSeriesInstanceUid() : "Missing UID") );
    }
  }

  if (studyItemID == vtkMRMLSubjectHierarchyNode::INVALID_ITEM_ID)
  {
    studyItemID = shNode->GetItemByUID(vtkMRMLSubjectHierarchyConstants::GetDICOMUIDName(), rtReader->GetStudyInstanceUid());
    if (studyItemID != vtkMRMLSubjectHierarchyNode::INVALID_ITEM_ID)
    {
      // Add attributes for DICOM tags
      shNode->SetItemAttribute(studyItemID,
        vtkMRMLSubjectHierarchyConstants::GetDICOMStudyInstanceUIDTagName(), rtReader->GetStudyInstanceUid() );
      shNode->SetItemAttribute(studyItemID,
        vtkMRMLSubjectHierarchyConstants::GetDICOMStudyIDTagName(), rtReader->GetStudyId() );
      shNode->SetItemAttribute(studyItemID,
        vtkMRMLSubjectHierarchyConstants::GetDICOMStudyDescriptionAttributeName(), rtReader->GetStudyDescription() );
      shNode->SetItemAttribute(studyItemID,
        vtkMRMLSubjectHierarchyConstants::GetDICOMStudyDateAttributeName(), rtReader->GetStudyDate() );
      shNode->SetItemAttribute(studyItemID,
        vtkMRMLSubjectHierarchyConstants::GetDICOMStudyTimeAttributeName(), rtReader->GetStudyTime() );

      // Set item name
      std::string studyItemName = ( !SlicerRtCommon::IsStringNullOrEmpty(rtReader->GetStudyDescription())
        ? std::string(rtReader->GetStudyDescription())
        : SlicerRtCommon::DICOMRTIMPORT_NO_STUDY_DESCRIPTION );
      QSettings* settings = qSlicerApplication::application()->settingsDialog()->settings();
      bool displayStudyID = settings->value("SubjectHierarchy/DisplayStudyIDInSubjectHierarchyItemName").toBool();
      if ( displayStudyID && !SlicerRtCommon::IsStringNullOrEmpty(rtReader->GetStudyId()) )
      {
        studyItemName += " (" + std::string(rtReader->GetStudyId()) + ")";
      }
      bool displayStudyDate = settings->value("SubjectHierarchy/DisplayStudyDateInSubjectHierarchyItemName").toBool();
      if ( displayStudyDate && !SlicerRtCommon::IsStringNullOrEmpty(rtReader->GetStudyDate()) )
      {
        studyItemName += " (" + std::string(rtReader->GetStudyDate()) + ")";
      }
      shNode->SetItemName(studyItemID, studyItemName);
    }
    else
    {
      vtkErrorWithObjectMacro(this->External, "InsertSeriesInSubjectHierarchy: Study item has not been created for series with Instance UID "
        << (rtReader->GetSeriesInstanceUid() ? rtReader->GetSeriesInstanceUid() : "Missing UID") );
    }
  }

  if (seriesItemID != vtkMRMLSubjectHierarchyNode::INVALID_ITEM_ID)
  {
    // Add attributes for DICOM tags to the series hierarchy item
    shNode->SetItemAttribute(seriesItemID,
     vtkMRMLSubjectHierarchyConstants::GetDICOMSeriesModalityAttributeName(), rtReader->GetSeriesModality() );
    shNode->SetItemAttribute(seriesItemID,
      vtkMRMLSubjectHierarchyConstants::GetDICOMSeriesNumberAttributeName(), rtReader->GetSeriesNumber() );

    // Set SOP instance UID (RT objects are in one file so have one SOP instance UID per series)
    // TODO: This is not correct for RTIMAGE, which may have several instances of DRRs within the same series
    shNode->SetItemUID(seriesItemID, vtkMRMLSubjectHierarchyConstants::GetDICOMInstanceUIDName(), rtReader->GetSOPInstanceUID());
  }
  else
  {
    vtkErrorWithObjectMacro(this->External, "InsertSeriesInSubjectHierarchy: Failed to insert series with Instance UID "
      << (rtReader->GetSeriesInstanceUid() ? rtReader->GetSeriesInstanceUid() : "Missing UID") );
    return;
  }
}

//------------------------------------------------------------------------------
void vtkSlicerDicomRtImportExportModuleLogic::vtkInternal::SetupRtImageGeometry(vtkMRMLNode* node)
{
  vtkMRMLScalarVolumeNode* rtImageVolumeNode = vtkMRMLScalarVolumeNode::SafeDownCast(node);
  vtkMRMLRTBeamNode* beamNode = vtkMRMLRTBeamNode::SafeDownCast(node);
  vtkIdType rtImageShItemID = vtkMRMLSubjectHierarchyNode::INVALID_ITEM_ID;
  vtkIdType beamShItemID = vtkMRMLSubjectHierarchyNode::INVALID_ITEM_ID;

  vtkMRMLSubjectHierarchyNode* shNode = vtkMRMLSubjectHierarchyNode::GetSubjectHierarchyNode(this->External->GetMRMLScene());
  if (!shNode)
  {
    vtkErrorWithObjectMacro(this->External, "SetupRtImageGeometry: Failed to access subject hierarchy node");
    return;
  }

  // If the function is called from the LoadRtImage function with an RT image volume: find corresponding RT beam
  if (rtImageVolumeNode)
  {
    // Get subject hierarchy item for RT image
    rtImageShItemID = shNode->GetItemByDataNode(rtImageVolumeNode);
    if (rtImageShItemID == vtkMRMLSubjectHierarchyNode::INVALID_ITEM_ID)
    {
      vtkErrorWithObjectMacro(this->External, "SetupRtImageGeometry: Failed to retrieve valid subject hierarchy item for RT image '" << rtImageVolumeNode->GetName() << "'");
      return;
    }

    // Find referenced RT plan node
    std::string referencedPlanSopInstanceUid = shNode->GetItemAttribute(
      rtImageShItemID, vtkMRMLSubjectHierarchyConstants::GetDICOMReferencedInstanceUIDsAttributeName() );
    if (referencedPlanSopInstanceUid.empty())
    {
      vtkErrorWithObjectMacro(this->External, "SetupRtImageGeometry: Unable to find referenced plan SOP instance UID for RT image '" << rtImageVolumeNode->GetName() << "'");
      return;
    }
    vtkIdType planShItemID = shNode->GetItemByUID(vtkMRMLSubjectHierarchyConstants::GetDICOMInstanceUIDName(), referencedPlanSopInstanceUid.c_str());
    if (planShItemID == vtkMRMLSubjectHierarchyNode::INVALID_ITEM_ID)
    {
      vtkDebugWithObjectMacro(this->External, "SetupRtImageGeometry: Cannot set up geometry of RT image '" << rtImageVolumeNode->GetName()
        << "' without the referenced RT plan. Will be set up upon loading the related plan");
      return;
    }
    vtkMRMLRTPlanNode* planNode = vtkMRMLRTPlanNode::SafeDownCast(shNode->GetItemDataNode(planShItemID));

    // Get referenced beam number
    std::string referencedBeamNumberStr = shNode->GetItemAttribute(rtImageShItemID, SlicerRtCommon::DICOMRTIMPORT_BEAM_NUMBER_ATTRIBUTE_NAME);
    if (referencedBeamNumberStr.empty())
    {
      vtkErrorWithObjectMacro(this->External, "SetupRtImageGeometry: No referenced beam number specified in RT image '" << rtImageVolumeNode->GetName() << "'");
      return;
    }
    int referencedBeamNumber = vtkVariant(referencedBeamNumberStr).ToInt();

    // Get beam according to referenced beam number
    beamNode = planNode->GetBeamByNumber(referencedBeamNumber);
    if (!beamNode)
    {
      vtkErrorWithObjectMacro(this->External, "SetupRtImageGeometry: Failed to retrieve beam node for RT image '" << rtImageVolumeNode->GetName() << "' in RT plan '" << shNode->GetItemName(planShItemID) << "'");
      return;
    }
  }
  // If the function is called from the LoadRtPlan function with a beam: find corresponding RT image
  else if (beamNode)
  {
    // Get RT plan for beam
    vtkMRMLRTPlanNode *planNode = beamNode->GetParentPlanNode();
    if (!planNode)
    {
      vtkErrorWithObjectMacro(this->External, "SetupRtImageGeometry: Failed to retrieve valid plan node for beam '" << beamNode->GetName() << "'");
      return;
    }
    vtkIdType planShItemID = planNode->GetPlanSubjectHierarchyItemID();
    if (planShItemID == vtkMRMLSubjectHierarchyNode::INVALID_ITEM_ID)
    {
      vtkErrorWithObjectMacro(this->External, "SetupRtImageGeometry: Failed to retrieve valid plan subject hierarchy item for beam '" << beamNode->GetName() << "'");
      return;
    }
    std::string rtPlanSopInstanceUid = shNode->GetItemUID(planShItemID, vtkMRMLSubjectHierarchyConstants::GetDICOMInstanceUIDName());
    if (rtPlanSopInstanceUid.empty())
    {
      vtkErrorWithObjectMacro(this->External, "SetupRtImageGeometry: Failed to get RT Plan DICOM UID for beam '" << beamNode->GetName() << "'");
      return;
    }

    // Get isocenter beam number
    int beamNumber = beamNode->GetBeamNumber();
    // Get number of beams in the plan (if there is only one, then the beam number may nor be correctly referenced, so we cannot find it that way
    bool oneBeamInPlan = (shNode->GetNumberOfItemChildren(planShItemID) == 1);

    // Find corresponding RT image according to beam (isocenter) UID
    std::vector<vtkIdType> itemIDs;
    shNode->GetItemChildren(shNode->GetSceneItemID(), itemIDs, true);
    for (std::vector<vtkIdType>::iterator itemIt=itemIDs.begin(); itemIt!=itemIDs.end(); ++itemIt)
    {
      vtkIdType currentShItemID = (*itemIt);
      bool currentShItemReferencesPlan = false;
      vtkMRMLNode* associatedNode = shNode->GetItemDataNode(currentShItemID);
      if (associatedNode && associatedNode->IsA("vtkMRMLScalarVolumeNode")
        && !shNode->GetItemAttribute(currentShItemID, SlicerRtCommon::DICOMRTIMPORT_RTIMAGE_IDENTIFIER_ATTRIBUTE_NAME).empty() )
      {
        // If current item is the subject hierarchy item of an RT image, then determine it references the RT plan by DICOM
        std::vector<vtkIdType> referencedShItemIDs = shNode->GetItemsReferencedFromItemByDICOM(currentShItemID);
        for (std::vector<vtkIdType>::iterator refIt=referencedShItemIDs.begin(); refIt!=referencedShItemIDs.end(); ++refIt)
        {
          if ((*refIt) == planShItemID)
          {
            currentShItemReferencesPlan = true;
            break;
          }
        }

        // If RT image item references plan, then it is the corresponding RT image if beam numbers match
        if (currentShItemReferencesPlan)
        {
          // Get RT image referenced beam number
          int referencedBeamNumber = vtkVariant(shNode->GetItemAttribute(currentShItemID, SlicerRtCommon::DICOMRTIMPORT_BEAM_NUMBER_ATTRIBUTE_NAME)).ToInt();
          // If the referenced beam number matches the isocenter beam number, or if there is one beam in the plan, then we found the RT image
          if (referencedBeamNumber == beamNumber || oneBeamInPlan)
          {
            rtImageVolumeNode = vtkMRMLScalarVolumeNode::SafeDownCast(shNode->GetItemDataNode(currentShItemID));
            rtImageShItemID = currentShItemID;
            break;
          }
        }
      }

      // Return if a referenced displayed model is present for the RT image, because it means that the geometry has been set up successfully before
      if (rtImageVolumeNode)
      {
        vtkMRMLModelNode* modelNode = vtkMRMLModelNode::SafeDownCast(
          rtImageVolumeNode->GetNodeReference(vtkMRMLPlanarImageNode::PLANARIMAGE_DISPLAYED_MODEL_REFERENCE_ROLE.c_str()) );
        if (modelNode)
        {
          vtkDebugWithObjectMacro(this->External, "SetupRtImageGeometry: RT image '" << rtImageVolumeNode->GetName() << "' belonging to beam '" << beamNode->GetName() << "' seems to have been set up already.");
          return;
        }
      }
    }

    if (!rtImageVolumeNode)
    {
      // RT image for the isocenter is not loaded yet. Geometry will be set up upon loading the related RT image
      vtkDebugWithObjectMacro(this->External, "SetupRtImageGeometry: Cannot set up geometry of RT image corresponding to beam '" << beamNode->GetName()
        << "' because the RT image is not loaded yet. Will be set up upon loading the related RT image");
      return;
    }
  }
  else
  {
    vtkErrorWithObjectMacro(this->External, "SetupRtImageGeometry: Input node is neither a volume node nor an plan POIs markups fiducial node");
    return;
  }

  // We have both the RT image and the isocenter, we can set up the geometry

  // Get source to RT image plane distance (along beam axis)
  double rtImageSid = 0.0;
  std::string rtImageSidStr = shNode->GetItemAttribute(rtImageShItemID, SlicerRtCommon::DICOMRTIMPORT_RTIMAGE_SID_ATTRIBUTE_NAME);
  if (!rtImageSidStr.empty())
  {
    rtImageSid = vtkVariant(rtImageSidStr).ToDouble();
  }
  // Get RT image position (the x and y coordinates (in mm) of the upper left hand corner of the image, in the IEC X-RAY IMAGE RECEPTOR coordinate system)
  double rtImagePosition[2] = {0.0, 0.0};
  std::string rtImagePositionStr = shNode->GetItemAttribute(rtImageShItemID, SlicerRtCommon::DICOMRTIMPORT_RTIMAGE_POSITION_ATTRIBUTE_NAME);
  if (!rtImagePositionStr.empty())
  {
    std::stringstream ss;
    ss << rtImagePositionStr;
    ss >> rtImagePosition[0] >> rtImagePosition[1];
  }

  // Extract beam-related parameters needed to compute RT image coordinate system
  double sourceAxisDistance = beamNode->GetSAD();
  double gantryAngle = beamNode->GetGantryAngle();
  double couchAngle = beamNode->GetCouchAngle();

  // Get isocenter coordinates
  double isocenterWorldCoordinates[3] = {0.0, 0.0, 0.0};
  if (!beamNode->GetPlanIsocenterPosition(isocenterWorldCoordinates))
  {
    vtkErrorWithObjectMacro(this->External, "SetupRtImageGeometry: Failed to get plan isocenter position");
    return;
  }

  // Assemble transform from isocenter IEC to RT image RAS
  vtkSmartPointer<vtkTransform> fixedToIsocenterTransform = vtkSmartPointer<vtkTransform>::New();
  fixedToIsocenterTransform->Identity();
  fixedToIsocenterTransform->Translate(isocenterWorldCoordinates);

  vtkSmartPointer<vtkTransform> couchToFixedTransform = vtkSmartPointer<vtkTransform>::New();
  couchToFixedTransform->Identity();
  couchToFixedTransform->RotateWXYZ((-1.0)*couchAngle, 0.0, 1.0, 0.0);

  vtkSmartPointer<vtkTransform> gantryToCouchTransform = vtkSmartPointer<vtkTransform>::New();
  gantryToCouchTransform->Identity();
  gantryToCouchTransform->RotateWXYZ(gantryAngle, 0.0, 0.0, 1.0);

  vtkSmartPointer<vtkTransform> sourceToGantryTransform = vtkSmartPointer<vtkTransform>::New();
  sourceToGantryTransform->Identity();
  sourceToGantryTransform->Translate(0.0, sourceAxisDistance, 0.0);

  vtkSmartPointer<vtkTransform> rtImageToSourceTransform = vtkSmartPointer<vtkTransform>::New();
  rtImageToSourceTransform->Identity();
  rtImageToSourceTransform->Translate(0.0, -rtImageSid, 0.0);

  vtkSmartPointer<vtkTransform> rtImageCenterToCornerTransform = vtkSmartPointer<vtkTransform>::New();
  rtImageCenterToCornerTransform->Identity();
  rtImageCenterToCornerTransform->Translate(-rtImagePosition[0], 0.0, rtImagePosition[1]);

  // Create isocenter to RAS transform
  // The transformation below is based section C.8.8 in DICOM standard volume 3:
  // "Note: IEC document 62C/269/CDV 'Amendment to IEC 61217: Radiotherapy Equipment -
  //  Coordinates, movements and scales' also defines a patient-based coordinate system, and
  //  specifies the relationship between the DICOM Patient Coordinate System (see Section
  //  C.7.6.2.1.1) and the IEC PATIENT Coordinate System. Rotating the IEC PATIENT Coordinate
  //  System described in IEC 62C/269/CDV (1999) by 90 degrees counter-clockwise (in the negative
  //  direction) about the x-axis yields the DICOM Patient Coordinate System, i.e. (XDICOM, YDICOM,
  //  ZDICOM) = (XIEC, -ZIEC, YIEC). Refer to the latest IEC documentation for the current definition of the
  //  IEC PATIENT Coordinate System."
  // The IJK to RAS transform already contains the LPS to RAS conversion, so we only need to consider this rotation
  vtkSmartPointer<vtkTransform> iecToLpsTransform = vtkSmartPointer<vtkTransform>::New();
  iecToLpsTransform->Identity();
  iecToLpsTransform->RotateX(90.0);

  // Get RT image IJK to RAS matrix (containing the spacing and the LPS-RAS conversion)
  vtkSmartPointer<vtkMatrix4x4> rtImageIjkToRtImageRasTransformMatrix = vtkSmartPointer<vtkMatrix4x4>::New();
  rtImageVolumeNode->GetIJKToRASMatrix(rtImageIjkToRtImageRasTransformMatrix);
  vtkSmartPointer<vtkTransform> rtImageIjkToRtImageRasTransform = vtkSmartPointer<vtkTransform>::New();
  rtImageIjkToRtImageRasTransform->SetMatrix(rtImageIjkToRtImageRasTransformMatrix);

  // Concatenate the transform components
  vtkSmartPointer<vtkTransform> isocenterToRtImageRas = vtkSmartPointer<vtkTransform>::New();
  isocenterToRtImageRas->Identity();
  isocenterToRtImageRas->PreMultiply();
  isocenterToRtImageRas->Concatenate(fixedToIsocenterTransform);
  isocenterToRtImageRas->Concatenate(couchToFixedTransform);
  isocenterToRtImageRas->Concatenate(gantryToCouchTransform);
  isocenterToRtImageRas->Concatenate(sourceToGantryTransform);
  isocenterToRtImageRas->Concatenate(rtImageToSourceTransform);
  isocenterToRtImageRas->Concatenate(rtImageCenterToCornerTransform);
  isocenterToRtImageRas->Concatenate(iecToLpsTransform); // LPS = IJK
  isocenterToRtImageRas->Concatenate(rtImageIjkToRtImageRasTransformMatrix);

  // Transform RT image to proper position and orientation
  rtImageVolumeNode->SetIJKToRASMatrix(isocenterToRtImageRas->GetMatrix());

  // Set up outputs for the planar image display
  vtkSmartPointer<vtkMRMLModelNode> displayedModelNode = vtkSmartPointer<vtkMRMLModelNode>::New();
  this->External->GetMRMLScene()->AddNode(displayedModelNode);
  std::string displayedModelNodeName = vtkMRMLPlanarImageNode::PLANARIMAGE_MODEL_NODE_NAME_PREFIX + std::string(rtImageVolumeNode->GetName());
  displayedModelNode->SetName(displayedModelNodeName.c_str());
  displayedModelNode->SetAttribute(vtkMRMLSubjectHierarchyConstants::GetSubjectHierarchyExcludeFromTreeAttributeName().c_str(), "1");

  // Create PlanarImage parameter set node
  std::string planarImageParameterSetNodeName;
  planarImageParameterSetNodeName = this->External->GetMRMLScene()->GenerateUniqueName(
    vtkMRMLPlanarImageNode::PLANARIMAGE_PARAMETER_SET_BASE_NAME_PREFIX + std::string(rtImageVolumeNode->GetName()) );
  vtkSmartPointer<vtkMRMLPlanarImageNode> planarImageParameterSetNode = vtkSmartPointer<vtkMRMLPlanarImageNode>::New();
  planarImageParameterSetNode->SetName(planarImageParameterSetNodeName.c_str());
  this->External->GetMRMLScene()->AddNode(planarImageParameterSetNode);
  planarImageParameterSetNode->SetAndObserveRtImageVolumeNode(rtImageVolumeNode);
  planarImageParameterSetNode->SetAndObserveDisplayedModelNode(displayedModelNode);

  // Create planar image model for the RT image
  this->External->PlanarImageLogic->CreateModelForPlanarImage(planarImageParameterSetNode);

  // Hide the displayed planar image model by default
  displayedModelNode->SetDisplayVisibility(0);
}


//----------------------------------------------------------------------------
// vtkSlicerDicomRtImportExportModuleLogic methods

//----------------------------------------------------------------------------
vtkSlicerDicomRtImportExportModuleLogic::vtkSlicerDicomRtImportExportModuleLogic()
{
  this->Internal = new vtkInternal(this);

  this->IsodoseLogic = NULL;
  this->PlanarImageLogic = NULL;
  this->BeamsLogic = NULL;

  this->BeamModelsInSeparateBranch = true;
}

//----------------------------------------------------------------------------
vtkSlicerDicomRtImportExportModuleLogic::~vtkSlicerDicomRtImportExportModuleLogic()
{
  this->SetIsodoseLogic(NULL);
  this->SetPlanarImageLogic(NULL);
  this->SetBeamsLogic(NULL);

  if (this->Internal)
  {
    delete this->Internal;
    this->Internal = NULL;
  }
}

//----------------------------------------------------------------------------
void vtkSlicerDicomRtImportExportModuleLogic::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}

//---------------------------------------------------------------------------
void vtkSlicerDicomRtImportExportModuleLogic::SetMRMLSceneInternal(vtkMRMLScene * newScene)
{
  vtkSmartPointer<vtkIntArray> events = vtkSmartPointer<vtkIntArray>::New();
  events->InsertNextValue(vtkMRMLScene::EndCloseEvent);
  this->SetAndObserveMRMLSceneEvents(newScene, events.GetPointer());
}

//---------------------------------------------------------------------------
void vtkSlicerDicomRtImportExportModuleLogic::OnMRMLSceneEndClose()
{
  if (!this->GetMRMLScene())
  {
    vtkErrorMacro("OnMRMLSceneEndClose: Invalid MRML scene");
    return;
  }
}

//-----------------------------------------------------------------------------
void vtkSlicerDicomRtImportExportModuleLogic::RegisterNodes()
{
  if (!this->GetMRMLScene())
  {
    vtkErrorMacro("RegisterNodes: Invalid MRML scene");
    return;
  }

  // Register converter rules
  vtkSegmentationConverterFactory::GetInstance()->RegisterConverterRule(
    vtkSmartPointer<vtkRibbonModelToBinaryLabelmapConversionRule>::New() );
  vtkSegmentationConverterFactory::GetInstance()->RegisterConverterRule(
    vtkSmartPointer<vtkPlanarContourToRibbonModelConversionRule>::New() );
  vtkSegmentationConverterFactory::GetInstance()->RegisterConverterRule(
    vtkSmartPointer<vtkPlanarContourToClosedSurfaceConversionRule>::New() );

}

//---------------------------------------------------------------------------
void vtkSlicerDicomRtImportExportModuleLogic::ExamineForLoad(vtkStringArray* fileList, vtkCollection* loadables)
{
  if (!fileList || !loadables)
  {
    return;
  }
  loadables->RemoveAllItems();

  for (int fileIndex=0; fileIndex<fileList->GetNumberOfValues(); ++fileIndex)
  {
    // Load file in DCMTK
    DcmFileFormat fileformat;
    vtkStdString fileName = fileList->GetValue(fileIndex);
    OFCondition result = fileformat.loadFile(fileName.c_str(), EXS_Unknown);
    if (!result.good())
    {
      continue; // Failed to parse this file, skip it
    }

    // Check SOP Class UID for one of the supported RT objects
    DcmDataset *dataset = fileformat.getDataset();
    OFString sopClass;
    if (!dataset->findAndGetOFString(DCM_SOPClassUID, sopClass).good() || sopClass.empty())
    {
      continue; // Failed to parse this file, skip it
    }

    // DICOM parsing is successful, now check if the object is loadable
    OFString name("");
    OFString seriesNumber("");
    std::vector<OFString> referencedSOPInstanceUIDs;
    dataset->findAndGetOFString(DCM_SeriesNumber, seriesNumber);
    if (!seriesNumber.empty())
    {
      name += seriesNumber + ": ";
    }

    // RTDose
    if (sopClass == UID_RTDoseStorage)
    {
      this->Internal->ExamineRtDoseDataset(dataset, name, referencedSOPInstanceUIDs);
    }
    // RTPlan
    else if (sopClass == UID_RTPlanStorage)
    {
      this->Internal->ExamineRtPlanDataset(dataset, name, referencedSOPInstanceUIDs);
    }
    // RTStructureSet
    else if (sopClass == UID_RTStructureSetStorage)
    {
      this->Internal->ExamineRtStructureSetDataset(dataset, name, referencedSOPInstanceUIDs);
    }
    // RTImage
    else if (sopClass == UID_RTImageStorage)
    {
      this->Internal->ExamineRtImageDataset(dataset, name, referencedSOPInstanceUIDs);
    }
    /* Not yet supported
    else if (sopClass == UID_RTTreatmentSummaryRecordStorage)
    else if (sopClass == UID_RTIonPlanStorage)
    else if (sopClass == UID_RTIonBeamsTreatmentRecordStorage)
    */
    else
    {
      continue; // Not an RT file
    }

    // The file is a loadable RT object, create and set up loadable
    vtkSmartPointer<vtkSlicerDICOMLoadable> loadable = vtkSmartPointer<vtkSlicerDICOMLoadable>::New();
    loadable->SetName(name.c_str());
    loadable->AddFile(fileName.c_str());
    loadable->SetConfidence(1.0);
    loadable->SetSelected(true);
    std::vector<OFString>::iterator uidIt;
    for (uidIt = referencedSOPInstanceUIDs.begin(); uidIt != referencedSOPInstanceUIDs.end(); ++uidIt)
    {
      loadable->AddReferencedInstanceUID(uidIt->c_str());
    }
    loadables->AddItem(loadable);
  }
}

//---------------------------------------------------------------------------
bool vtkSlicerDicomRtImportExportModuleLogic::LoadDicomRT(vtkSlicerDICOMLoadable* loadable)
{
  bool loadSuccessful = false;

  if (!loadable || loadable->GetFiles()->GetNumberOfValues() < 1 || loadable->GetConfidence() == 0.0)
  {
    vtkErrorMacro("LoadDicomRT: Unable to load DICOM-RT data due to invalid loadable information");
    return loadSuccessful;
  }

  const char* firstFileName = loadable->GetFiles()->GetValue(0);

  std::cout << "Loading series '" << loadable->GetName() << "' from file '" << firstFileName << "'" << std::endl;

  vtkSmartPointer<vtkSlicerDicomRtReader> rtReader = vtkSmartPointer<vtkSlicerDicomRtReader>::New();
  rtReader->SetFileName(firstFileName);
  rtReader->Update();

  // One series can contain composite information, e.g, an RTPLAN series can contain structure sets and plans as well
  // TODO: vtkSlicerDicomRtReader class does not support this yet

  // RTSTRUCT
  if (rtReader->GetLoadRTStructureSetSuccessful())
  {
    loadSuccessful = this->Internal->LoadRtStructureSet(rtReader, loadable);
  }

  // RTDOSE
  if (rtReader->GetLoadRTDoseSuccessful())
  {
    loadSuccessful = this->Internal->LoadRtDose(rtReader, loadable);
  }

  // RTPLAN
  if (rtReader->GetLoadRTPlanSuccessful())
  {
    loadSuccessful = this->Internal->LoadRtPlan(rtReader, loadable);
  }

  // RTIMAGE
  if (rtReader->GetLoadRTImageSuccessful())
  {
    loadSuccessful = this->Internal->LoadRtImage(rtReader, loadable);
  }

  return loadSuccessful;
}

//----------------------------------------------------------------------------
std::string vtkSlicerDicomRtImportExportModuleLogic::ExportDicomRTStudy(vtkCollection* exportables)
{
  std::string error("");
  vtkMRMLScene* mrmlScene = this->GetMRMLScene();
  if (!mrmlScene)
  {
    error = "MRML scene not valid";
    vtkErrorMacro("ExportDicomRTStudy: " + error);
    return error;
  }
  vtkMRMLSubjectHierarchyNode* shNode = vtkMRMLSubjectHierarchyNode::GetSubjectHierarchyNode(this->GetMRMLScene());
  if (!shNode)
  {
    error = "Failed to access subject hierarchy node";
    vtkErrorMacro("ExportDicomRTStudy: " + error);
    return error;
  }

  if (exportables->GetNumberOfItems() < 1)
  {
    error = "Exportable list contains no exportables";
    vtkErrorMacro("ExportDicomRTStudy: " + error);
    return error;
  }

  // Get common export parameters from first exportable
  // These are the ones available through the DICOM Export widget
  vtkSlicerDICOMExportable* firstExportable = vtkSlicerDICOMExportable::SafeDownCast(exportables->GetItemAsObject(0));
  const char* patientName = firstExportable->GetTag(vtkMRMLSubjectHierarchyConstants::GetDICOMPatientNameTagName().c_str());
  const char* patientID = firstExportable->GetTag(vtkMRMLSubjectHierarchyConstants::GetDICOMPatientIDTagName().c_str());
  const char* patientSex = firstExportable->GetTag(vtkMRMLSubjectHierarchyConstants::GetDICOMPatientSexTagName().c_str());
  const char* studyDate = firstExportable->GetTag(vtkMRMLSubjectHierarchyConstants::GetDICOMStudyDateTagName().c_str());
  const char* studyTime = firstExportable->GetTag(vtkMRMLSubjectHierarchyConstants::GetDICOMStudyTimeTagName().c_str());
  const char* studyDescription = firstExportable->GetTag(vtkMRMLSubjectHierarchyConstants::GetDICOMStudyDescriptionTagName().c_str());
  if (studyDescription && !strcmp(studyDescription, "No study description"))
  {
    studyDescription = 0;
  }
  const char* imageSeriesDescription = 0;
  const char* imageSeriesNumber = 0;
  const char* imageSeriesModality = 0;
  const char* doseSeriesDescription = 0;
  const char* doseSeriesNumber = 0;
  const char* rtssSeriesDescription = 0;
  const char* rtssSeriesNumber = 0;

  // Get other common export parameters
  // These are the ones available in hierarchy
  std::string studyInstanceUid = "";
  std::string studyID = "";
  vtkIdType firstShItemID = firstExportable->GetSubjectHierarchyItemID();
  if (firstShItemID != vtkMRMLSubjectHierarchyNode::INVALID_ITEM_ID)
  {
    vtkIdType studyItemID = shNode->GetItemAncestorAtLevel(firstShItemID, vtkMRMLSubjectHierarchyConstants::GetDICOMLevelStudy());
    if (studyItemID != vtkMRMLSubjectHierarchyNode::INVALID_ITEM_ID)
    {
      studyInstanceUid = shNode->GetItemUID(studyItemID, vtkMRMLSubjectHierarchyConstants::GetDICOMUIDName());
      studyID = shNode->GetItemAttribute(studyItemID, vtkMRMLSubjectHierarchyConstants::GetDICOMStudyIDTagName());
    }
    else
    {
      vtkWarningMacro("ExportDicomRTStudy: Failed to get ancestor study from exportable with subject hierarchy item ID " + firstExportable->GetSubjectHierarchyItemID());
    }
  }
  else
  {
    vtkWarningMacro("ExportDicomRTStudy: Failed to get SH item from exportable with item ID " + firstExportable->GetSubjectHierarchyItemID());
  }

  const char* outputPath = firstExportable->GetDirectory();

  // Get nodes for the different roles from the exportable list
  vtkMRMLScalarVolumeNode* doseNode = NULL;
  vtkMRMLSegmentationNode* segmentationNode = NULL;
  vtkMRMLScalarVolumeNode* imageNode = NULL;
  std::vector<std::string> imageSliceUIDs;
  for (int index=0; index<exportables->GetNumberOfItems(); ++index)
  {
    vtkSlicerDICOMExportable* exportable = vtkSlicerDICOMExportable::SafeDownCast(
      exportables->GetItemAsObject(index) );
    vtkIdType shItemID = exportable->GetSubjectHierarchyItemID();
    if (shItemID == vtkMRMLSubjectHierarchyNode::INVALID_ITEM_ID)
    {
      vtkWarningMacro("ExportDicomRTStudy: Failed to get item from exportable with item ID " + exportable->GetSubjectHierarchyItemID());
      // There might be enough exportables for a successful export, all roles are checked later
      continue;
    }
    vtkMRMLNode* associatedNode = shNode->GetItemDataNode(shItemID);

    // GCS FIX TODO: The below logic seems to allow only a single dose,
    // single image, and single segmentation per study.
    // However, there is no check to enforce this.

    // Check if dose volume and set it if found
    if (associatedNode && SlicerRtCommon::IsDoseVolumeNode(associatedNode))
    {
      doseNode = vtkMRMLScalarVolumeNode::SafeDownCast(associatedNode);

      doseSeriesDescription = exportable->GetTag("SeriesDescription");
      if (doseSeriesDescription && !strcmp(doseSeriesDescription, "No series description"))
      {
        doseSeriesDescription = 0;
      }
      doseSeriesNumber = exportable->GetTag("SeriesNumber");
    }
    // Check if segmentation node and set if found
    else if (associatedNode && associatedNode->IsA("vtkMRMLSegmentationNode"))
    {
      segmentationNode = vtkMRMLSegmentationNode::SafeDownCast(associatedNode);

      rtssSeriesDescription = exportable->GetTag("SeriesDescription");
      if (rtssSeriesDescription && !strcmp(rtssSeriesDescription, "No series description"))
      {
        rtssSeriesDescription = 0;
      }
      rtssSeriesNumber = exportable->GetTag("SeriesNumber");
    }
    // Check if other volume (anatomical volume role) and set if found
    else if (associatedNode && associatedNode->IsA("vtkMRMLScalarVolumeNode"))
    {
      imageNode = vtkMRMLScalarVolumeNode::SafeDownCast(associatedNode);

      // Get series DICOM tags to export
      imageSeriesDescription = exportable->GetTag("SeriesDescription");
      if (imageSeriesDescription && !strcmp(imageSeriesDescription, "No series description"))
      {
        imageSeriesDescription = 0;
      }
      //TODO: Getter function adds "DICOM." prefix (which is for attribute names), while the exportable tags are without that
      // imageSeriesModality = exportable->GetTag(vtkMRMLSubjectHierarchyConstants::GetDICOMSeriesModalityAttributeName());
      imageSeriesModality = exportable->GetTag("Modality");
      // imageSeriesNumber = exportable->GetTag(vtkMRMLSubjectHierarchyConstants::GetDICOMSeriesNumberAttributeName());
      imageSeriesNumber = exportable->GetTag("SeriesNumber");

      // Get slice instance UIDs
      std::string sliceInstanceUIDList = shNode->GetItemUID(shItemID, vtkMRMLSubjectHierarchyConstants::GetDICOMInstanceUIDName());
      vtkMRMLSubjectHierarchyNode::DeserializeUIDList(sliceInstanceUIDList, imageSliceUIDs);
    }
    // Report warning if a node cannot be assigned a role
    else
    {
      vtkWarningMacro("ExportDicomRTStudy: Unable to assign supported RT role to exported item " + shNode->GetItemName(shItemID));
    }
  }

  // Make sure there is an image node.  Don't check for struct / dose, as those are optional
  if (!imageNode)
  {
    error = "Must export the primary anatomical (CT/MR) image";
    vtkErrorMacro("ExportDicomRTStudy: " + error);
    return error;
  }

  // Create RT writer
  vtkSmartPointer<vtkSlicerDicomRtWriter> rtWriter = vtkSmartPointer<vtkSlicerDicomRtWriter>::New();

  // Set study-level metadata
  rtWriter->SetPatientName(patientName);
  rtWriter->SetPatientID(patientID);
  rtWriter->SetPatientSex(patientSex);
  rtWriter->SetStudyDate(studyDate);
  rtWriter->SetStudyTime(studyTime);
  rtWriter->SetStudyDescription(studyDescription);
  rtWriter->SetStudyInstanceUid(studyInstanceUid.c_str());
  rtWriter->SetStudyID(studyID.c_str());

  // Set image-level metadata
  rtWriter->SetImageSeriesDescription(imageSeriesDescription);
  rtWriter->SetImageSeriesNumber(imageSeriesNumber);
  rtWriter->SetImageSeriesModality(imageSeriesModality);
  rtWriter->SetDoseSeriesDescription(doseSeriesDescription);
  rtWriter->SetDoseSeriesNumber(doseSeriesNumber);
  rtWriter->SetRtssSeriesDescription(rtssSeriesDescription);
  rtWriter->SetRtssSeriesNumber(rtssSeriesNumber);

  // Convert input image (CT/MR/etc) to the format Plastimatch can use
  vtkSmartPointer<vtkOrientedImageData> imageOrientedImageData = vtkSmartPointer<vtkOrientedImageData>::New();
  if (!SlicerRtCommon::ConvertVolumeNodeToVtkOrientedImageData(imageNode, imageOrientedImageData))
  {
    error = "Failed to convert anatomical image " + std::string(imageNode->GetName()) + " to oriented image data";
    vtkErrorMacro("ExportDicomRTStudy: " + error);
    return error;
  }
  // Need to resample image data if its transform contains shear
  vtkSmartPointer<vtkMatrix4x4> imageToWorldMatrix = vtkSmartPointer<vtkMatrix4x4>::New();
  imageOrientedImageData->GetImageToWorldMatrix(imageToWorldMatrix);
  if (vtkOrientedImageDataResample::DoesTransformMatrixContainShear(imageToWorldMatrix))
  {
    vtkSmartPointer<vtkTransform> imageToWorldTransform = vtkSmartPointer<vtkTransform>::New();
    imageToWorldTransform->SetMatrix(imageToWorldMatrix);
    vtkOrientedImageDataResample::TransformOrientedImage(imageOrientedImageData, imageToWorldTransform, false, true);
    // Set identity transform to image data so that it is at the same location
    vtkSmartPointer<vtkMatrix4x4> identityMatrix = vtkSmartPointer<vtkMatrix4x4>::New();
    identityMatrix->Identity();
    imageOrientedImageData->SetGeometryFromImageToWorldMatrix(identityMatrix);
  }
  // Set anatomical image to RT writer
  Plm_image::Pointer plm_img = PlmCommon::ConvertVtkOrientedImageDataToPlmImage(imageOrientedImageData);
  if (plm_img->dim(0) * plm_img->dim(1) * plm_img->dim(2) == 0)
  {
    error = "Failed to convert anatomical (CT/MR) image to Plastimatch format";
    vtkErrorMacro("ExportDicomRTStudy: " + error);
    return error;
  }
  rtWriter->SetImage(plm_img);

  // Convert input RTDose to the format Plastimatch can use
  if (doseNode)
  {
    vtkSmartPointer<vtkOrientedImageData> doseOrientedImageData = vtkSmartPointer<vtkOrientedImageData>::New();
    if (!SlicerRtCommon::ConvertVolumeNodeToVtkOrientedImageData(doseNode, doseOrientedImageData))
    {
      error = "Failed to convert dose volume " + std::string(doseNode->GetName()) + " to oriented image data";
      vtkErrorMacro("ExportDicomRTStudy: " + error);
      return error;
    }
    // Need to resample image data if its transform contains shear
    vtkSmartPointer<vtkMatrix4x4> doseToWorldMatrix = vtkSmartPointer<vtkMatrix4x4>::New();
    doseOrientedImageData->GetImageToWorldMatrix(doseToWorldMatrix);
    if (vtkOrientedImageDataResample::DoesTransformMatrixContainShear(doseToWorldMatrix))
    {
      vtkSmartPointer<vtkTransform> doseToWorldTransform = vtkSmartPointer<vtkTransform>::New();
      doseToWorldTransform->SetMatrix(doseToWorldMatrix);
      vtkOrientedImageDataResample::TransformOrientedImage(doseOrientedImageData, doseToWorldTransform, false, true);
      // Set identity transform to image data so that it is at the same location
      vtkSmartPointer<vtkMatrix4x4> identityMatrix = vtkSmartPointer<vtkMatrix4x4>::New();
      identityMatrix->Identity();
      doseOrientedImageData->SetGeometryFromImageToWorldMatrix(identityMatrix);
    }
    // Set anatomical image to RT writer
    Plm_image::Pointer dose_img = PlmCommon::ConvertVtkOrientedImageDataToPlmImage(doseOrientedImageData);
    if (dose_img->dim(0) * dose_img->dim(1) * dose_img->dim(2) == 0)
    {
      error = "Failed to convert dose volume to Plastimatch format";
      vtkErrorMacro("ExportDicomRTStudy: " + error);
      return error;
    }
    rtWriter->SetDose(dose_img);
  }

  // Convert input segmentation to the format Plastimatch can use
  if (segmentationNode)
  {
    // If master representation is labelmap type, then export binary labelmap
    vtkSegmentation* segmentation = segmentationNode->GetSegmentation();
    if (segmentation->IsMasterRepresentationImageData())
    {
      // Make sure segmentation contains binary labelmap
      if ( !segmentationNode->GetSegmentation()->CreateRepresentation(
        vtkSegmentationConverter::GetSegmentationBinaryLabelmapRepresentationName() ) )
      {
        error = "Failed to get binary labelmap representation from segmentation " + std::string(segmentationNode->GetName());
        vtkErrorMacro("ExportDicomRTStudy: " + error);
        return error;
      }

      // Export each segment in segmentation
      std::vector< std::string > segmentIDs;
      segmentationNode->GetSegmentation()->GetSegmentIDs(segmentIDs);
      for (std::vector< std::string >::const_iterator segmentIdIt = segmentIDs.begin(); segmentIdIt != segmentIDs.end(); ++segmentIdIt)
      {
        std::string segmentID = *segmentIdIt;
        vtkSegment* segment = segmentationNode->GetSegmentation()->GetSegment(*segmentIdIt);

        // Get binary labelmap representation
        vtkOrientedImageData* binaryLabelmap = vtkOrientedImageData::SafeDownCast(
          segment->GetRepresentation(vtkSegmentationConverter::GetSegmentationBinaryLabelmapRepresentationName()) );
        if (!binaryLabelmap)
        {
          error = "Failed to get binary labelmap representation from segment " + segmentID;
          vtkErrorMacro("ExportDicomRTStudy: " + error);
          return error;
        }
        // Temporarily copy labelmap image data as it will be probably resampled
        vtkSmartPointer<vtkOrientedImageData> binaryLabelmapCopy = vtkSmartPointer<vtkOrientedImageData>::New();
        binaryLabelmapCopy->DeepCopy(binaryLabelmap);

        // Apply parent transformation nodes if necessary
        if (segmentationNode->GetParentTransformNode())
        {
          if (!vtkSlicerSegmentationsModuleLogic::ApplyParentTransformToOrientedImageData(segmentationNode, binaryLabelmapCopy))
          {
            std::string errorMessage("Failed to apply parent transformation to exported segment");
            vtkErrorMacro("ExportDicomRTStudy: " << errorMessage);
            return errorMessage;
          }
        }
        // Make sure the labelmap dimensions match the reference dimensions
        if ( !vtkOrientedImageDataResample::DoGeometriesMatch(imageOrientedImageData, binaryLabelmapCopy)
          || !vtkOrientedImageDataResample::DoExtentsMatch(imageOrientedImageData, binaryLabelmapCopy) )
        {
          if (!vtkOrientedImageDataResample::ResampleOrientedImageToReferenceOrientedImage(binaryLabelmapCopy, imageOrientedImageData, binaryLabelmapCopy))
          {
            error = "Failed to resample segment " + segmentID + " to match anatomical image geometry";
            vtkErrorMacro("ExportDicomRTStudy: " + error);
            return error;
          }
        }

        // Convert mask to Plm image
        Plm_image::Pointer plmStructure = PlmCommon::ConvertVtkOrientedImageDataToPlmImage(binaryLabelmapCopy);
        if (!plmStructure)
        {
          error = "Failed to convert segment labelmap " + segmentID + " to Plastimatch image";
          vtkErrorMacro("ExportDicomRTStudy: " + error);
          return error;
        }

        // Get segment properties
        std::string segmentName = segment->GetName();
        double* segmentColor = segment->GetColor();

        rtWriter->AddStructure(plmStructure->itk_uchar(), segmentName.c_str(), segmentColor);
      } // For each segment
    }
    // If master representation is poly data type, then export from closed surface
    else if (segmentation->IsMasterRepresentationPolyData())
    {
      // Make sure segmentation contains closed surface
      if ( !segmentationNode->GetSegmentation()->CreateRepresentation(
        vtkSegmentationConverter::GetSegmentationClosedSurfaceRepresentationName() ) )
      {
        error = "Failed to get closed surface representation from segmentation " + std::string(segmentationNode->GetName());
        vtkErrorMacro("ExportDicomRTStudy: " + error);
        return error;
      }

      // Get transform  from segmentation to world (RAS)
      vtkSmartPointer<vtkGeneralTransform> nodeToWorldTransform = vtkSmartPointer<vtkGeneralTransform>::New();
      nodeToWorldTransform->Identity();
      if (segmentationNode->GetParentTransformNode())
      {
        segmentationNode->GetParentTransformNode()->GetTransformToWorld(nodeToWorldTransform);
      }
      // Initialize poly data transformer
      vtkSmartPointer<vtkTransformPolyDataFilter> transformPolyData = vtkSmartPointer<vtkTransformPolyDataFilter>::New();
      transformPolyData->SetTransform(nodeToWorldTransform);

      // Initialize cutting plane with normal of the Z axis of the anatomical image
      vtkSmartPointer<vtkMatrix4x4> imageToWorldMatrix = vtkSmartPointer<vtkMatrix4x4>::New();
      imageOrientedImageData->GetImageToWorldMatrix(imageToWorldMatrix);
      double normal[3] = { imageToWorldMatrix->GetElement(0,2), imageToWorldMatrix->GetElement(1,2), imageToWorldMatrix->GetElement(2,2) };
      vtkSmartPointer<vtkPlane> slicePlane = vtkSmartPointer<vtkPlane>::New();
      slicePlane->SetNormal(normal);

      // Export each segment in segmentation
      std::vector< std::string > segmentIDs;
      segmentationNode->GetSegmentation()->GetSegmentIDs(segmentIDs);
      for (std::vector< std::string >::const_iterator segmentIdIt = segmentIDs.begin(); segmentIdIt != segmentIDs.end(); ++segmentIdIt)
      {
        std::string segmentID = *segmentIdIt;
        vtkSegment* segment = segmentationNode->GetSegmentation()->GetSegment(*segmentIdIt);

        // Get closed surface representation
        vtkPolyData* closedSurfacePolyData = vtkPolyData::SafeDownCast(
          segment->GetRepresentation(vtkSegmentationConverter::GetSegmentationClosedSurfaceRepresentationName()) );
        if (!closedSurfacePolyData)
        {
          error = "Failed to get closed surface representation from segment " + segmentID;
          vtkErrorMacro("ExportDicomRTStudy: " + error);
          return error;
        }

        // Initialize cutter pipeline for segment
        transformPolyData->SetInputData(closedSurfacePolyData);
        vtkSmartPointer<vtkCutter> cutter = vtkSmartPointer<vtkCutter>::New();
        cutter->SetInputConnection(transformPolyData->GetOutputPort());
        cutter->SetGenerateCutScalars(0);
        vtkSmartPointer<vtkStripper> stripper = vtkSmartPointer<vtkStripper>::New();
        stripper->SetInputConnection(cutter->GetOutputPort());

        // Get segment bounding box
        double bounds[6] = {0.0,0.0,0.0,0.0,0.0,0.0};
        transformPolyData->Update();
        transformPolyData->GetOutput()->GetBounds(bounds);

        // Containers to be passed to the writer
        std::vector<int> sliceNumbers;
        std::vector<std::string> sliceUIDs;
        std::vector<vtkPolyData*> sliceContours;

        // Create planar contours from closed surface based on each of the anatomical image slices
        int imageExtent[6] = {0,-1,0,-1,0,-1};
        imageOrientedImageData->GetExtent(imageExtent);
        for (int slice=imageExtent[4]; slice<imageExtent[5]; ++slice)
        {
          // Calculate slice origin
          double origin[3] = { imageToWorldMatrix->GetElement(0,3) + slice*normal[0],
                               imageToWorldMatrix->GetElement(1,3) + slice*normal[1],
                               imageToWorldMatrix->GetElement(2,3) + slice*normal[2] };
          slicePlane->SetOrigin(origin);
          if (origin[2] < bounds[4] || origin[2] > bounds[5])
          {
            // No contours outside surface bounds
            continue;
          }

          // Cut closed surface at slice
          cutter->SetCutFunction(slicePlane);

          // Get instance UID of corresponding slice
          int sliceNumber = slice-imageExtent[0];
          sliceNumbers.push_back(sliceNumber);
          std::string sliceInstanceUID = (imageSliceUIDs.size() > sliceNumber ? imageSliceUIDs[sliceNumber] : "");
          sliceUIDs.push_back(sliceInstanceUID);

          // Save slice contour
          stripper->Update();
          vtkPolyData* sliceContour = vtkPolyData::New();
          sliceContour->SetPoints(stripper->GetOutput()->GetPoints());
          sliceContour->SetPolys(stripper->GetOutput()->GetLines());
          sliceContours.push_back(sliceContour);
        } // For each anatomical image slice

        // Get segment properties
        std::string segmentName = segment->GetName();
        double* segmentColor = segment->GetColor();

        // Add contours to writer
        rtWriter->AddStructure(segmentName.c_str(), segmentColor, sliceNumbers, sliceUIDs, sliceContours);

        // Clean up slice contours
        for (std::vector<vtkPolyData*>::iterator contourIt=sliceContours.begin(); contourIt!=sliceContours.end(); ++contourIt)
        {
          (*contourIt)->Delete();
        }
      } // For each segment
    }
    else
    {
      error = "Structure set contains unsupported master representation";
      vtkErrorMacro("ExportDicomRTStudy: " + error);
      return error;
    }
  }

  // Write files to disk
  rtWriter->SetFileName(outputPath);
  rtWriter->Write();

  // Success (error is empty string)
  return error;
}

//-----------------------------------------------------------------------------
vtkMRMLScalarVolumeNode* vtkSlicerDicomRtImportExportModuleLogic::GetReferencedVolumeByDicomForSegmentation(vtkMRMLSegmentationNode* segmentationNode)
{
  if (!segmentationNode)
  {
    return NULL;
  }
  vtkMRMLSubjectHierarchyNode* shNode = vtkMRMLSubjectHierarchyNode::GetSubjectHierarchyNode(segmentationNode->GetScene());
  if (!shNode)
  {
    vtkErrorWithObjectMacro(segmentationNode, "GetReferencedVolumeByDicomForSegmentation: Failed to access subject hierarchy node");
    return NULL;
  }

  // Get referenced series UID for segmentation
  vtkIdType segmentationShItemID = shNode->GetItemByDataNode(segmentationNode);
  std::string referencedSeriesUid = shNode->GetItemAttribute(segmentationShItemID, SlicerRtCommon::DICOMRTIMPORT_ROI_REFERENCED_SERIES_UID_ATTRIBUTE_NAME);
  if (referencedSeriesUid.empty())
  {
    vtkErrorWithObjectMacro(segmentationNode, "No referenced series UID found for segmentation '" << segmentationNode->GetName() << "'");
    return NULL;
  }

  // Get referenced volume subject hierarchy item by found UID
  vtkIdType referencedSeriesShItemID = shNode->GetItemByUID(vtkMRMLSubjectHierarchyConstants::GetDICOMUIDName(), referencedSeriesUid.c_str());
  if (referencedSeriesShItemID == vtkMRMLSubjectHierarchyNode::INVALID_ITEM_ID)
  {
    return NULL;
  }

  // Get and return referenced volume
  return vtkMRMLScalarVolumeNode::SafeDownCast(shNode->GetItemDataNode(referencedSeriesShItemID));
}
