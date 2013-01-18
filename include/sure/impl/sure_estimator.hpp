// Software License Agreement (BSD License)
//
// Copyright (c) 2012, Fraunhofer FKIE/US
// All rights reserved.
// Author: Torsten Fiolka
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above
//    copyright notice, this list of conditions and the following
//    disclaimer in the documentation and/or other materials provided
//    with the distribution.
//  * Neither the name of Fraunhofer FKIE nor the names of its
//    contributors may be used to endorse or promote products derived
//    from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
// FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
// COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
// INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
// BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
// LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
// ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

float sure::calculateCornerness(const std::vector<OctreeNode*>& nodes)
{
  Eigen::Vector3f mean = Eigen::Vector3f::Zero(), direction;
  float sumWeight = 0.f;
  int count = 0;
  Eigen::Matrix3f covMatrix = Eigen::Matrix3f::Zero();

  for(unsigned int j=0; j<nodes.size(); ++j)
  {
    if( nodes[j]->value.entropy != 0.f )
    {
      mean[0] += nodes[j]->value.entropy * nodes[j]->closestPosition.p[0];
      mean[1] += nodes[j]->value.entropy * nodes[j]->closestPosition.p[1];
      mean[2] += nodes[j]->value.entropy * nodes[j]->closestPosition.p[2];

      sumWeight += nodes[j]->value.entropy;
      count++;
    }
  }
  if( sumWeight > 0.f )
  {
    mean /= sumWeight;
  }
  else
  {
    return INFINITY;
  }
  for(unsigned int j=0; j<nodes.size(); ++j)
  {
    if( nodes[j]->value.entropy != 0.f )
    {
      direction[0] = (mean[0] - nodes[j]->closestPosition.p[0]);
      direction[1] = (mean[1] - nodes[j]->closestPosition.p[1]);
      direction[2] = (mean[2] - nodes[j]->closestPosition.p[2]);

      covMatrix += nodes[j]->value.entropy * (direction * direction.transpose());
    }
  }
  covMatrix /= sumWeight;

  Eigen::Matrix<float, 3, 1> eigenValues;
  Eigen::Matrix<float, 3, 3> eigenVectors;
  pcl::eigen33(covMatrix, eigenVectors, eigenValues);
  return eigenValues(0) / eigenValues(2);
}

//! orientates a normal to the viewpoint
void sure::orientateNormal(Eigen::Vector3f& normal, const Eigen::Vector3f& point)
{
  float dotProduct = normal.dot(point);
  unsigned int index = 0;
  while( fabs(dotProduct) < 1e-1 && index < 7 )
  {
    Eigen::Vector3f farPoint = Eigen::Vector3f::Zero();
    if( index % 2 == 0 )
    {
      farPoint[0] = 1000.f;
    }
    if( (index / 2) % 2 == 0 )
    {
      farPoint[1] = 1000.f;
    }
    if( index % 4 == 0 )
    {
      farPoint[2] = 1000.f;
    }
    dotProduct = normal.dot(farPoint);
    index++;
  }
  if( dotProduct < 0.f )
  {
    normal[0] = -normal[0];
    normal[1] = -normal[1];
    normal[2] = -normal[2];
  }
}



//
//  Methods for data and organisational purposes
//

//! resets the current object
template <typename PointT>
void sure::SURE_Estimator<PointT>::reset()
{
  if( octree )
    delete octree;
  octree = NULL;
  octreeDepth = 0;
  octreeNodeSizeByDepth.clear();
  config = sure::Configuration();
  features.clear();
  rangeImage.reset();
  octreeMap.clear();
  addedPoints.clear();
//  viewPoint = Eigen::Vector3f::Zero();
}

/**
 * Sets a new maximum octree size
 * @param size
 */
template <typename PointT>
void sure::SURE_Estimator<PointT>::resize(unsigned int size)
{
  reset();
  if( size != currentOctreeSize && size > 0 )
  {
    currentOctreeSize = size;
    if( octreeAllocator )
    {
      delete octreeAllocator;
    }
    octreeAllocator = new OctreeAllocator(currentOctreeSize);
    histogramAllocator.reset((float) currentOctreeSize * sure::PERCENTAGE_OF_NORMAL_HISTOGRAMS);
  }
}

//
//  Getters and Setters
//

template <typename PointT>
sure::OctreeNode* sure::SURE_Estimator<PointT>::getNodeFromPosition(const PointT& point, int level)
{
  OctreePosition pos;
  pos.p[0] = point.x;
  pos.p[1] = point.y;
  pos.p[2] = point.z;
  return getNodeFromPosition(pos, level);
}

template <typename PointT>
sure::OctreeNode* sure::SURE_Estimator<PointT>::getNodeFromPosition(const Eigen::Vector3f& posVec, int level)
{
  sure::OctreePosition pos;
  pos.p[0] = posVec[0];
  pos.p[1] = posVec[1];
  pos.p[2] = posVec[2];
  return getNodeFromPosition(pos, level);
}

/**
 * Returns the octree node containing a given position on a given level, or the closest node, if the position lies not within the octree
 * @param pos
 * @param level
 * @return
 */
template <typename PointT>
sure::OctreeNode* sure::SURE_Estimator<PointT>::getNodeFromPosition(const sure::OctreePosition& pos, int level)
{
  sure::OctreeNode* closest = NULL;
  float distance, bestDistance = INFINITY;
  if( level == 0 )
  {
    level = config.samplingLevel;
  }
  for(unsigned int i=0; i<octreeMap[level].size(); ++i)
  {
    if( octreeMap[level][i]->inRegion(pos) )
    {
      return octreeMap[level][i];
    }
  }
  for(unsigned int i=0; i<octreeMap[level].size(); ++i)
  {
    distance = (pos.p[0] - octreeMap[level][i]->closestPosition.p[0])*(pos.p[0] - octreeMap[level][i]->closestPosition.p[0]) + (pos.p[1] - octreeMap[level][i]->closestPosition.p[1])*(pos.p[1] - octreeMap[level][i]->closestPosition.p[1]) + (pos.p[2] - octreeMap[level][i]->closestPosition.p[2])*(pos.p[2] - octreeMap[level][i]->closestPosition.p[2]);
    if( distance < bestDistance )
    {
      bestDistance = distance;
      closest = octreeMap[level][i];
    }
  }
  return closest;
}

template <typename PointT>
pcl::PointCloud<pcl::InterestPoint>::Ptr sure::SURE_Estimator<PointT>::getInterestPoints() const
{
  pcl::PointCloud<pcl::InterestPoint>::Ptr interestPoints(new pcl::PointCloud<pcl::InterestPoint>);
  interestPoints->reserve(features.size());
  for(std::vector<sure::Feature>::const_iterator it=features.begin(); it != features.end(); ++it)
  {
    pcl::InterestPoint p;
    p.x = (*it).point[0];
    p.y = (*it).point[1];
    p.z = (*it).point[2];
    p.strength = (*it).entropy;
    interestPoints->points.push_back(p);
  }
  interestPoints->header = input_->header;
  return interestPoints;
}

//
//  Methodes for the Octree
//

/**
 * Inserts a point into the octree with given status (used for inserting artificial points, since they must not be used for description)
 * @param p
 * @param status
 */
template <typename PointT>
void sure::SURE_Estimator<PointT>::insertPointInOctree(const PointT& p, sure::OctreeValue::NodeStatus status)
{
  sure::OctreeNode* n = NULL;
  float volumeSize, distance;

  sure::OctreePoint point;
  point.value.summedPos[0] = point.position.p[0] = p.x;
  point.value.summedPos[1] = point.position.p[1] = p.y;
  point.value.summedPos[2] = point.position.p[2] = p.z;

  point.value.summedSquares[0] = point.position.p[0] * point.position.p[0]; // xx
  point.value.summedSquares[1] = point.value.summedSquares[3] = point.position.p[0] * point.position.p[1]; // xy
  point.value.summedSquares[2] = point.value.summedSquares[6] = point.position.p[0] * point.position.p[2]; // xz
  point.value.summedSquares[4] = point.position.p[1] * point.position.p[1]; // yy
  point.value.summedSquares[5] = point.value.summedSquares[7] = point.position.p[1] * point.position.p[2]; // yz
  point.value.summedSquares[8] = point.position.p[2] * point.position.p[2]; // zz

  point.value.numberOfPoints = 1;
  point.value.statusOfMaximum = status;

  sure::convertPCLRGBtoFloatRGB(p.rgb, point.value.colorR, point.value.colorG, point.value.colorB);

  if( config.limitOctreeResolution )
  {
    distance = (input_->sensor_origin_(0) - point.position.p[0]) * (input_->sensor_origin_(0) - point.position.p[0])
             + (input_->sensor_origin_(1) - point.position.p[1]) * (input_->sensor_origin_(1) - point.position.p[1])
             + (input_->sensor_origin_(2) - point.position.p[2]) * (input_->sensor_origin_(2) - point.position.p[2]);
    volumeSize = std::max(config.minimumOctreeVolumeSize, sure::OCTREE_ACCURACY_SETOFF * distance);
    n = octree->root->addPoint(point, volumeSize);
  }
  else
  {
    n = octree->addPoint(point);
  }

  if( n )
  {
    octreeDepth = std::max(octreeDepth, n->depth);
  }
}

//! (re)builds the octree and the octree sampling map
template <typename PointT>
void sure::SURE_Estimator<PointT>::buildOctree()
{
  if( octree )
    delete octree;
  if( octreeAllocator )
    octreeAllocator->reset();
  OctreePosition origin, maximum;
  origin.p[0] = origin.p[1] = origin.p[2] = 0.f;
  maximum.p[0] = maximum.p[1] = maximum.p[2] = sure::OCTREE_INITIAL_SIZE;
  octree = new Octree(maximum, origin, sure::OCTREE_MINIMUM_VOLUME_SIZE, octreeAllocator);

  octreeDepth = 0;

  if( config.ignoreBackgroundDetections || config.additionalPointsOnDepthBorders )
  {
    rangeImage.setInputCloud(input_);
    rangeImage.setIndices(indices_);
    rangeImage.calculateRangeImage();
  }

//  if( config.ignoreBackgroundBorders )
//  {
//    for(unsigned int i=0; i<cloud.size(); ++i)
//    {
//      rangeImage.isBackgroundBorder(i) ? inserPointInOctree(cloud[i], sure::OctreeValue::BACKGROUND_NODE) : inserPointInOctree(cloud[i]);
//    }
//  }
//  else
  {
    for(unsigned int i=0; i<indices_->size(); ++i)
    {
      const PointT& point = input_->points[indices_->at(i)];
      if( std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z) )
      {
        insertPointInOctree(input_->points[indices_->at(i)]);
      }
    }
  }

  if( config.additionalPointsOnDepthBorders )
  {
    continueDepthBorders();
  }

  octreeNodeSizeByDepth.clear();
  octreeNodeSizeByDepth.resize(octreeDepth+1);

  resampleOctreeSamplingMap();
}

//! (re)build the Octree Sampling Map
template <typename PointT>
inline void sure::SURE_Estimator<PointT>::resampleOctreeSamplingMap()
{
  octreeMap.clear();
  octreeMap = algorithm::downsampleOcTree(*octree, false, octreeDepth);

  octreeSize = 0;
  float avgVolumeSize = 0.f;
  for(unsigned int level=0; level<=octreeDepth; ++level)
  {
    avgVolumeSize = 0.f;
    for(unsigned int j=0; j<octreeMap[level].size(); ++j)
    {
      avgVolumeSize += octreeMap[level][j]->maxPosition.p[0] - octreeMap[level][j]->minPosition.p[0];
    }
    octreeNodeSizeByDepth[level] = avgVolumeSize / (float) octreeMap[level].size();
    octreeSize += octreeMap[level].size();
  }
}

template <typename PointT>
void sure::SURE_Estimator<PointT>::continueDepthBorders()
{
  rangeImage.addPointsOnBorders(config.minimumOctreeVolumeSize, config.histogramSize, addedPoints);
  for(unsigned int i=0; i<addedPoints.size(); ++i)
  {
    insertPointInOctree(addedPoints[i], sure::OctreeValue::ARTIFICIAL);
  }
}


//
// Normal calculation
//

template <typename PointT>
void sure::SURE_Estimator<PointT>::calculateNormals()
{
  int count = 0;
  count += octreeMap[config.normalSamplingLevel].size();
  bool inNode = config.normalScale < octreeNodeSizeByDepth[config.normalSamplingLevel];

//#pragma omp parallel for schedule(dynamic)
  for(int i=0; i<(int) octreeMap[config.normalSamplingLevel].size(); ++i)
  {
    if( octreeMap[config.normalSamplingLevel][i]->value.statusOfNormal != sure::OctreeValue::NORMAL_NOT_CALCULATED )
    {
      continue;
    }
    if( !octreeMap[config.normalSamplingLevel][i]->value.normalHistogram )
    {
      octreeMap[config.normalSamplingLevel][i]->value.normalHistogram = histogramAllocator.allocate();
    }
    if( inNode )
    {
      calculateNormal(octreeMap[config.normalSamplingLevel][i]);
    }
    else
    {
      calculateNormal(octreeMap[config.normalSamplingLevel][i], config.normalScaleRadius, sure::OCTREE_MINIMUM_VOLUME_SIZE);
    }
  }
}

template <typename PointT>
bool sure::SURE_Estimator<PointT>::calculateNormal(sure::OctreeValue& value, const unsigned int count, float normal[3], const OctreePosition& pos, sure::NormalHistogram* histogram)
{
  Eigen::Vector3f position = Eigen::Vector3f(pos.p[0], pos.p[1], pos.p[2]);
  return calculateNormal(value, count, normal, position, histogram);
}


/**
 * Calculates a normal
 * @param value the value class with information for the covariance matrix
 * @param count the number of the points in value object
 * @param normal reference for the calculated normal
 * @param histogram reference for the histogram into which the normal is stored
 * @param pos position of the normal for view vector calculation
 * @return true, if the calculation is succesful
 */
template <typename PointT>
bool sure::SURE_Estimator<PointT>::calculateNormal(sure::OctreeValue& value, const unsigned int count, float normal[3], const Eigen::Vector3f& pos, sure::NormalHistogram* histogram)
{
  if( count >= sure::MINIMUM_POINTS_FOR_NORMAL )
  {

    Eigen::Matrix3f summedSquares;
    Eigen::Vector3f summedPosition;

    summedSquares(0, 0) = value.summedSquares[0] * (1.f / (float) count);
    summedSquares(0, 1) = value.summedSquares[1] * (1.f / (float) count);
    summedSquares(0, 2) = value.summedSquares[2] * (1.f / (float) count);
    summedSquares(1, 0) = value.summedSquares[3] * (1.f / (float) count);
    summedSquares(1, 1) = value.summedSquares[4] * (1.f / (float) count);
    summedSquares(1, 2) = value.summedSquares[5] * (1.f / (float) count);
    summedSquares(2, 0) = value.summedSquares[6] * (1.f / (float) count);
    summedSquares(2, 1) = value.summedSquares[7] * (1.f / (float) count);
    summedSquares(2, 2) = value.summedSquares[8] * (1.f / (float) count);

    summedPosition(0) = value.summedPos[0] * (1.f / (float) count);
    summedPosition(1) = value.summedPos[1] * (1.f / (float) count);
    summedPosition(2) = value.summedPos[2] * (1.f / (float) count);

    summedSquares -= summedPosition * summedPosition.transpose();
    summedSquares *= 1.f / (float) count;

    float eigenValue;

    if( !sure::is_finite(summedSquares) )
    {
      return false;
    }

    Eigen::Vector3f eigenVector;
    pcl::eigen33(summedSquares, eigenValue, eigenVector);

    if( std::isfinite(eigenVector[0]) && std::isfinite(eigenVector[1]) && std::isfinite(eigenVector[2]) )
    {
      eigenVector.normalize();
      Eigen::Vector3f orientationVec = Eigen::Vector3f(input_->sensor_origin_[0], input_->sensor_origin_[1], input_->sensor_origin_[2])-pos;
      sure::orientateNormal(eigenVector, orientationVec);
      if( histogram )
      {
        histogram->calculateHistogram(eigenVector, count);
      }
      normal[0] = eigenVector[0];
      normal[1] = eigenVector[1];
      normal[2] = eigenVector[2];
      value.statusOfNormal = sure::OctreeValue::NORMAL_STABLE;
      return true;
    }
    else
    {
      normal[0] = normal[1] = normal[2] = 0.f;
    }
  }
  return false;
}

/**
 * Calculates a normal with the values of a given octree node
 * @param node
 */
template <typename PointT>
bool sure::SURE_Estimator<PointT>::calculateNormal(OctreeNode* node)
{
  return calculateNormal(node->value, node->numPoints, node->value.normal, node->closestPosition, node->value.normalHistogram);
}

/**
 * Calculates a normal on a given node with a given radius
 * @param node
 * @param radius
 * @param minResolution
 */
template <typename PointT>
bool sure::SURE_Estimator<PointT>::calculateNormal(OctreeNode* node, float radius, float minResolution)
{
  OctreePosition minPosition, maxPosition;

  minPosition.p[0] = node->closestPosition.p[0] - radius;
  minPosition.p[1] = node->closestPosition.p[1] - radius;
  minPosition.p[2] = node->closestPosition.p[2] - radius;

  maxPosition.p[0] = node->closestPosition.p[0] + radius;
  maxPosition.p[1] = node->closestPosition.p[1] + radius;
  maxPosition.p[2] = node->closestPosition.p[2] + radius;

  sure::OctreeValue value;
  unsigned int count = 0;
  octree->getValueAndCountInVolume(value, count, minPosition, maxPosition, minResolution);

  if( calculateNormal(value, count, node->value.normal, node->closestPosition, node->value.normalHistogram) )
  {
    node->value.statusOfNormal = value.statusOfNormal;
    return true;
  }
  node->value.statusOfNormal = sure::OctreeValue::NORMAL_UNSTABLE;
  return false;
}

/**
 * Calculates a normal on a given position with a given radius
 * @param position
 * @param radius
 * @param normal
 * @param minResolution
 * @return
 */
template <typename PointT>
bool sure::SURE_Estimator<PointT>::calculateNormal(const OctreePosition& position, float radius, float normal[3], float minResolution)
{
  OctreePosition minPosition, maxPosition;
  Eigen::Vector3f posVec = Eigen::Vector3f(position.p[0], position.p[1], position.p[2]);

  minPosition.p[0] = position.p[0] - radius;
  minPosition.p[1] = position.p[1] - radius;
  minPosition.p[2] = position.p[2] - radius;

  maxPosition.p[0] = position.p[0] + radius;
  maxPosition.p[1] = position.p[1] + radius;
  maxPosition.p[2] = position.p[2] + radius;

  sure::OctreeValue value(0);
  value.statusOfMaximum = sure::OctreeValue::ARTIFICIAL;
  unsigned int count = 0;
  octree->getValueAndCountInVolume(value, count, minPosition, maxPosition, minResolution);

  return calculateNormal(value, count, normal, posVec, NULL);
}

template <typename PointT>
bool sure::SURE_Estimator<PointT>::calculateNormal(const Eigen::Vector3f& position, float radius, Eigen::Vector3f& normal, float minResolution)
{
  OctreePosition minPosition, maxPosition;

  minPosition.p[0] = position[0] - radius;
  minPosition.p[1] = position[1] - radius;
  minPosition.p[2] = position[2] - radius;

  maxPosition.p[0] = position[0] + radius;
  maxPosition.p[1] = position[1] + radius;
  maxPosition.p[2] = position[2] + radius;

  sure::OctreeValue value;
  value.statusOfMaximum = sure::OctreeValue::ARTIFICIAL;
  unsigned int count = 0;
  octree->getValueAndCountInVolume(value, count, minPosition, maxPosition, minResolution);

  if( calculateNormal(value, count, value.normal, position, NULL) )
  {
    normal = Eigen::Vector3f(value.normal[0], value.normal[1], value.normal[2]);
    return true;
  }
  else
  {
    normal = Eigen::Vector3f::Zero();
    return false;
  }
}

//
//  Feature calculation
//

/**
 * Main method for creating features. It calculates normal-histograms and entropy, searchs for local maxima and
 * extracts features
 */
template <typename PointT>
void sure::SURE_Estimator<PointT>::calculateFeatures()
{
  initCompute();
  std::cout << "Pointcloud: " << input_->size() << " Indices: " << indices_->size() << std::endl;
  features.clear();
  histogramAllocator.clear();

  std::cout << "Octree";
  buildOctree();

  std::cout << "(" << octreeSize << ") Normals";
  calculateNormals();

  std::cout << " Entropy";
  calculateEntropy();

  std::cout << " Extraction";
  extractFeature();

  if( config.improvedLocalization )
  {
    std::cout << " Localization";
    localizeFeatureWithMeanShift(3);
  }
  std::cout << std::endl << "Calculated " << this->features.size() << " features."<< std::endl;
}

//! Calculates entropy for all nodes on the configured depth
template <typename PointT>
void sure::SURE_Estimator<PointT>::calculateEntropy()
{
  int level = config.samplingLevel;

  for(unsigned int i=0; i<octreeMap[level].size(); ++i)
  {
    if( octreeMap[level][i]->numPoints > 0 )
    {
      switch( config.entropyMode )
      {
        default:
        case sure::Configuration::NORMALS:
          calculateNormalEntropy(octreeMap[level][i]);
          break;
        case sure::Configuration::CROSSPRODUCTS_ALL_NORMALS_WITH_MAIN_NORMAL:
          calculateCrossProductEntropy(octreeMap[level][i]);
          break;
        case sure::Configuration::CROSSPRODUCTS_ALL_NORMALS_PAIRWISE:
          calculatePairwiseCrossProductEntropy(octreeMap[level][i]);
          break;
      }
    }
  }
}

//! Calculates the entropy on a single node using a histogram of normals
template <typename PointT>
void sure::SURE_Estimator<PointT>::calculateNormalEntropy(OctreeNode* treenode)
{
  std::vector<OctreeNode*> listOfNodes;
  OctreePosition minPosition, maxPosition;

  if( treenode->value.entropyHistogram )
  {
    treenode->value.entropyHistogram->clear();
  }
  else
  {
    treenode->value.entropyHistogram = histogramAllocator.allocate();
  }

  minPosition.p[0] = treenode->closestPosition.p[0] - config.histogramRadius;
  minPosition.p[1] = treenode->closestPosition.p[1] - config.histogramRadius;
  minPosition.p[2] = treenode->closestPosition.p[2] - config.histogramRadius;
  maxPosition.p[0] = treenode->closestPosition.p[0] + config.histogramRadius;
  maxPosition.p[1] = treenode->closestPosition.p[1] + config.histogramRadius;
  maxPosition.p[2] = treenode->closestPosition.p[2] + config.histogramRadius;

  octree->root->getAllNodesInVolumeOnSamplingDepth(listOfNodes, minPosition, maxPosition, config.normalSamplingLevel, false);

  for(unsigned int i=0; i<listOfNodes.size(); ++i)
  {
    if( listOfNodes[i]->value.statusOfNormal == sure::OctreeValue::NORMAL_STABLE && listOfNodes[i]->value.normalHistogram )
    {
      (*treenode->value.entropyHistogram) += *(listOfNodes[i]->value.normalHistogram);
    }
  }
//  treenode->value.scale /= (config.histogramSize / config.normalSamplingRate) * (config.histogramSize / config.normalSamplingRate);
  treenode->value.entropyHistogram->calculateEntropy();
  treenode->value.entropy = treenode->value.entropyHistogram->entropy;
}

//! Calculates the entropy on a single node using a histogram of crossproducts between the center normal and surrounding normals
template <typename PointT>
void sure::SURE_Estimator<PointT>::calculateCrossProductEntropy(OctreeNode* treenode)
{
  std::vector<OctreeNode*> listOfNodes;
  OctreePosition minPosition, maxPosition;
  Eigen::Vector3f secNormal, refNormal;

  if( calculateNormal(treenode, config.histogramRadius) )
  {
    refNormal = Eigen::Vector3f(treenode->value.normal[0], treenode->value.normal[1], treenode->value.normal[2]);
  }
  else
  {
    return;
  }

  if( treenode->value.entropyHistogram )
  {
    treenode->value.entropyHistogram->clear();
  }
  else
  {
    treenode->value.entropyHistogram = histogramAllocator.allocate();
  }

  minPosition.p[0] = treenode->closestPosition.p[0] - config.histogramRadius;
  minPosition.p[1] = treenode->closestPosition.p[1] - config.histogramRadius;
  minPosition.p[2] = treenode->closestPosition.p[2] - config.histogramRadius;
  maxPosition.p[0] = treenode->closestPosition.p[0] + config.histogramRadius;
  maxPosition.p[1] = treenode->closestPosition.p[1] + config.histogramRadius;
  maxPosition.p[2] = treenode->closestPosition.p[2] + config.histogramRadius;

  octree->root->getAllNodesInVolumeOnSamplingDepth(listOfNodes, minPosition, maxPosition, config.normalSamplingLevel, false);

  for(unsigned int i=0; i<listOfNodes.size(); ++i)
  {
    if( listOfNodes[i]->value.statusOfNormal == sure::OctreeValue::NORMAL_STABLE && listOfNodes[i]->value.normalHistogram )
    {
      secNormal = Eigen::Vector3f(listOfNodes[i]->value.normal[0], listOfNodes[i]->value.normal[1], listOfNodes[i]->value.normal[2]);
      treenode->value.entropyHistogram->insertCrossProduct(refNormal, secNormal, (sure::NormalHistogram::WeightMethod) config.histogramWeightMethod);
    }
  }
  treenode->value.entropyHistogram->calculateEntropy();
  treenode->value.entropy = treenode->value.entropyHistogram->entropy;
}


//! Calculates the entropy on a single node using a histogram of crossproducts between the surrounding normals
template <typename PointT>
void sure::SURE_Estimator<PointT>::calculatePairwiseCrossProductEntropy(OctreeNode* treenode)
{
  std::vector<OctreeNode*> listOfNodes;
  OctreePosition minPosition, maxPosition;
  Eigen::Vector3f secNormal, refNormal;

  if( treenode->value.entropyHistogram )
  {
    treenode->value.entropyHistogram->clear();
  }
  else
  {
    treenode->value.entropyHistogram = histogramAllocator.allocate();
  }

  minPosition.p[0] = treenode->closestPosition.p[0] - config.histogramRadius;
  minPosition.p[1] = treenode->closestPosition.p[1] - config.histogramRadius;
  minPosition.p[2] = treenode->closestPosition.p[2] - config.histogramRadius;
  maxPosition.p[0] = treenode->closestPosition.p[0] + config.histogramRadius;
  maxPosition.p[1] = treenode->closestPosition.p[1] + config.histogramRadius;
  maxPosition.p[2] = treenode->closestPosition.p[2] + config.histogramRadius;

  octree->root->getAllNodesInVolumeOnSamplingDepth(listOfNodes, minPosition, maxPosition, config.normalSamplingLevel, false);

  for(unsigned int i=0; i<listOfNodes.size(); ++i)
  {
    for(unsigned int j=i+1; j<listOfNodes.size(); ++j)
    {
      if( listOfNodes[i]->value.statusOfNormal == sure::OctreeValue::NORMAL_STABLE && listOfNodes[j]->value.statusOfNormal == sure::OctreeValue::NORMAL_STABLE )
      {
        Eigen::Vector3f refNormal(listOfNodes[i]->value.normal[0], listOfNodes[i]->value.normal[1], listOfNodes[i]->value.normal[2]);
        Eigen::Vector3f secNormal(listOfNodes[j]->value.normal[0], listOfNodes[j]->value.normal[1], listOfNodes[j]->value.normal[2]);
        treenode->value.entropyHistogram->insertCrossProduct(refNormal, secNormal, (sure::NormalHistogram::WeightMethod) config.histogramWeightMethod);
      }
    }
  }
  treenode->value.entropyHistogram->calculateEntropy();
  treenode->value.entropy = treenode->value.entropyHistogram->entropy;
}

//! Calculates the internal feature map
//! searchs and marks local maxima
template <typename PointT>
void sure::SURE_Estimator<PointT>::extractFeature()
{
  OctreePosition minPosition, maxPosition;

  unsigned int& level = config.samplingLevel;

  std::vector< OctreeNode* > neighbours;
  neighbours.reserve(floor((config.histogramSize/config.samplingRate)*(config.histogramSize/config.samplingRate)*(config.histogramSize/config.samplingRate)));

  for(unsigned int i=0; i<octreeMap[level].size(); ++i)
  {
    OctreeNode* currentNode = octreeMap[level][i];
    if( currentNode->value.statusOfMaximum == sure::OctreeValue::ARTIFICIAL || currentNode->value.statusOfMaximum == sure::OctreeValue::BACKGROUND )
    {
      continue;
    }
    // Check for threshold
    if( currentNode->value.entropy < config.minimumEntropy )
    {
      currentNode->value.statusOfMaximum = sure::OctreeValue::MAXIMUM_NOT_POSSIBLE;
      continue;
    }

    if( currentNode->value.statusOfMaximum == sure::OctreeValue::MAXIMUM_NOT_CALCULATED && config.minimumCornerness3D > 0.f )
    {
      OctreeNode* currentNode = octreeMap[level][i];
      neighbours.clear();

      // Collect Neighbors
      minPosition.p[0] = currentNode->closestPosition.p[0] - config.histogramRadius;
      minPosition.p[1] = currentNode->closestPosition.p[1] - config.histogramRadius;
      minPosition.p[2] = currentNode->closestPosition.p[2] - config.histogramRadius;
      maxPosition.p[0] = currentNode->closestPosition.p[0] + config.histogramRadius;
      maxPosition.p[1] = currentNode->closestPosition.p[1] + config.histogramRadius;
      maxPosition.p[2] = currentNode->closestPosition.p[2] + config.histogramRadius;

      octree->root->getAllNodesInVolumeOnSamplingDepth(neighbours, minPosition, maxPosition, level, false);
      currentNode->value.cornerness3D = sure::calculateCornerness(neighbours);
      if( currentNode->value.cornerness3D < config.minimumCornerness3D )
      {
        currentNode->value.statusOfMaximum = sure::OctreeValue::MAXIMUM_NOT_POSSIBLE;
      }
      else
      {
        currentNode->value.statusOfMaximum = sure::OctreeValue::MAXIMUM_POSSIBLE;
      }
    }
  }

  for(unsigned int i=0; i<octreeMap[level].size(); ++i)
  {
    OctreeNode* currentNode = octreeMap[level][i];
    if( currentNode->value.statusOfMaximum != sure::OctreeValue::MAXIMUM_POSSIBLE)
    {
      continue;
    }

    neighbours.clear();

    minPosition.p[0] = currentNode->closestPosition.p[0] - config.featureInfluenceRadius;
    minPosition.p[1] = currentNode->closestPosition.p[1] - config.featureInfluenceRadius;
    minPosition.p[2] = currentNode->closestPosition.p[2] - config.featureInfluenceRadius;
    maxPosition.p[0] = currentNode->closestPosition.p[0] + config.featureInfluenceRadius;
    maxPosition.p[1] = currentNode->closestPosition.p[1] + config.featureInfluenceRadius;
    maxPosition.p[2] = currentNode->closestPosition.p[2] + config.featureInfluenceRadius;

    octree->root->getAllNodesInVolumeOnSamplingDepth(neighbours, minPosition, maxPosition, level, false);

    const float& referenceEntropy = currentNode->value.entropy;

    for(unsigned int j=0; j<neighbours.size(); ++j)
    {
      // Check neighbors for maximum
      if( neighbours[j] == currentNode )
      {
        continue;
      }
      if( neighbours[j]->value.statusOfMaximum == sure::OctreeValue::MAXIMUM_FOUND )
      {
        octreeMap[level][i]->value.statusOfMaximum = sure::OctreeValue::MAXIMUM_NOT_POSSIBLE;
        break;
      }
      if( neighbours[j]->value.statusOfMaximum == sure::OctreeValue::MAXIMUM_POSSIBLE && neighbours[j]->value.entropy > referenceEntropy )
      {
        octreeMap[level][i]->value.statusOfMaximum = sure::OctreeValue::MAXIMUM_NOT_POSSIBLE;
        break;
      }
    }

    // If feature is still marked, push to featureVector
    if( octreeMap[level][i]->value.statusOfMaximum == sure::OctreeValue::MAXIMUM_POSSIBLE )
    {
      octreeMap[level][i]->value.statusOfMaximum = sure::OctreeValue::MAXIMUM_FOUND;
      features.push_back(createFeature(octreeMap[level][i]));
    }
  }
}

//! improves localization with mean shift
//! Localizes the Features with Mean-Shift
template <typename PointT>
void sure::SURE_Estimator<PointT>::localizeFeatureWithMeanShift(int iterations)
{
//  ROS_DEBUG_NAMED("octree", "Starting feature localization improvement with mean shift in %i iterations...", iterations);

  unsigned int level = config.samplingLevel;
  float searchRadius = config.histogramRadius;

  std::vector<OctreeNode*> listOfNodes;
  listOfNodes.reserve(floor((config.histogramSize/config.samplingRate)*(config.histogramSize/config.samplingRate)*(config.histogramSize/config.samplingRate)));
  sure::Feature newFeature;
  OctreePosition minPosition, maxPosition, shiftedPosition;
  int count;
  float summedShift, mean, standardDeviation, summedMean, summedSquares, entropyDifference;


  for(unsigned int featureIndex=0; featureIndex<features.size(); ++featureIndex)
  {
    if( features[featureIndex].radius != config.histogramRadius )
    {
      continue;
    }

    listOfNodes.clear();
    newFeature = features[featureIndex];

    for(int iteration=0; iteration<iterations; ++iteration)
    {
      summedShift = 0.f, mean = 0.f, standardDeviation = 0.f, summedMean = 0.f, summedSquares = 0.f, entropyDifference = 0.f;

      shiftedPosition.p[0] = shiftedPosition.p[1] = shiftedPosition.p[2] = 0.f;
      minPosition.p[0] = newFeature.point[0] - searchRadius;
      minPosition.p[1] = newFeature.point[1] - searchRadius;
      minPosition.p[2] = newFeature.point[2] - searchRadius;
      maxPosition.p[0] = newFeature.point[0] + searchRadius;
      maxPosition.p[1] = newFeature.point[1] + searchRadius;
      maxPosition.p[2] = newFeature.point[2] + searchRadius;

      listOfNodes.clear();
      octree->root->getAllNodesInVolumeOnSamplingDepth(listOfNodes, minPosition, maxPosition, level, false);

      count = 0;

      for(unsigned int i=0; i<listOfNodes.size(); ++i)
      {
        if( listOfNodes[i]->value.statusOfMaximum == sure::OctreeValue::ARTIFICIAL )
        {
          continue;
        }
//        Eigen::Vector3f pos(listOfNodes[i]->closestPosition.p[0], listOfNodes[i]->closestPosition.p[1], listOfNodes[i]->closestPosition.p[2]);
//        if( (pos-featureVector[featureIndex].pos.point).norm() > config.histogramRadius )
//        {
//          continue;
//        }
        summedMean += listOfNodes[i]->value.entropy;
        summedSquares += listOfNodes[i]->value.entropy * listOfNodes[i]->value.entropy;
        count++;
      }

      if( count > 0 )
      {
        mean = summedMean / float(count);
        summedMean = summedMean*summedMean;
        standardDeviation = sqrt(fabs(summedSquares - ((1.f/float(count)) * summedMean)));
        for(unsigned int i=0; i<listOfNodes.size(); ++i)
        {
          entropyDifference = mean - listOfNodes[i]->value.entropy;
          shiftedPosition += listOfNodes[i]->closestPosition * exp(-0.5f*(entropyDifference*entropyDifference) / (standardDeviation*standardDeviation));
          summedShift += exp(-0.5f*(entropyDifference*entropyDifference) / (standardDeviation*standardDeviation));
        }
        if( summedShift != 0 )
        {
          shiftedPosition = shiftedPosition * (1.f / summedShift);
        }
        if( !isnan(shiftedPosition.p[0]) && !isnan(shiftedPosition.p[1]) && !isnan(shiftedPosition.p[2]) )
        {
  //        float distance = sqrt((shiftedPosition.p[0] - newFeature.pos.point[0])*(shiftedPosition.p[0] - newFeature.pos.point[0]) + (shiftedPosition.p[1] - newFeature.pos.point[1])*(shiftedPosition.p[1] - newFeature.pos.point[1]) + (shiftedPosition.p[2] - newFeature.pos.point[2])*(shiftedPosition.p[2] - newFeature.pos.point[2]));
  //        ROS_DEBUG_NAMED("octree", "Mean Shift - Iteration: %i\t Distance: %f", iteration, distance);
          newFeature.point[0] = shiftedPosition.p[0];
          newFeature.point[1] = shiftedPosition.p[1];
          newFeature.point[2] = shiftedPosition.p[2];
        }
      }
    }
    createDescriptor(newFeature);
    features[featureIndex] = newFeature;
  }
//  ROS_DEBUG_NAMED("octree", "Mean shift localization finished in %f seconds.", ros::Duration(ros::Time::now() - start).toSec());
}

//! Creates a feature given a node
//! Extracts a feature from a given octree node
template <typename PointT>
sure::Feature sure::SURE_Estimator<PointT>::createFeature(OctreeNode* node)
{
  sure::Feature newFeature;
  newFeature.pointCloudIndex = node->value.pointCloudIndex;
  newFeature.entropy = node->value.entropy;
  newFeature.point[0] = node->closestPosition.p[0];
  newFeature.point[1] = node->closestPosition.p[1];
  newFeature.point[2] = node->closestPosition.p[2];
  newFeature.radius = config.histogramRadius;
  createDescriptor(newFeature);
  return newFeature;
}

/**
 * Extracts a feature on a given position with the current settings, but does not calculate the entropy
 * @param point in xyz-coordinates
 * @return
 */
template <typename PointT>
sure::Feature sure::SURE_Estimator<PointT>::createFeature(const Eigen::Vector3f& point)
{
  OctreePosition minPosition, maxPosition, closestPosition;
  std::vector<OctreeNode* > neighbours;
  sure::OctreeValue value;
  sure::Feature nf;

  closestPosition.p[0] = point[0];
  closestPosition.p[1] = point[1];
  closestPosition.p[2] = point[2];
  minPosition.p[0] = point[0] - config.samplingRate / 2.f;
  minPosition.p[1] = point[1] - config.samplingRate / 2.f;
  minPosition.p[2] = point[2] - config.samplingRate / 2.f;
  maxPosition.p[0] = point[0] + config.samplingRate / 2.f;
  maxPosition.p[1] = point[1] + config.samplingRate / 2.f;
  maxPosition.p[2] = point[2] + config.samplingRate / 2.f;
  nf.point = point;
  nf.radius = config.histogramRadius;
  createDescriptor(nf);
  return nf;
}

//! Creates a feature for a point in the input point cloud

template <typename PointT>
sure::Feature sure::SURE_Estimator<PointT>::createFeature(int index)
{
  Eigen::Vector3f pos;
  sure::Feature feature;
  if( index >= 0 && index < (int) input_->points.size() && !isinf(input_->points[index].x) && !isinf(input_->points[index].y) && !isinf(input_->points[index].z) )
  {
    pos[0] = input_->points[index].x;
    pos[1] = input_->points[index].y;
    pos[2] = input_->points[index].z;
    feature.setColor(input_->points[index].rgb);
    feature = createFeature(pos);
    feature.pointCloudIndex = index;
  }
  return feature;
}

//! Creates the descriptor for a given interest point

template <typename PointT>
void sure::SURE_Estimator<PointT>::createDescriptor(sure::Feature& feature)
{
  OctreePosition minPosition, maxPosition;
  calculateNormal(feature.point, config.histogramRadius, feature.normal);
  minPosition.p[0] = feature.point[0] - config.histogramRadius;
  minPosition.p[1] = feature.point[1] - config.histogramRadius;
  minPosition.p[2] = feature.point[2] - config.histogramRadius;
  maxPosition.p[0] = feature.point[0] + config.histogramRadius;
  maxPosition.p[1] = feature.point[1] + config.histogramRadius;
  maxPosition.p[2] = feature.point[2] + config.histogramRadius;

  sure::OctreeValue value = octree->getValueInVolume(minPosition, maxPosition);
  feature.setColor(value.r(), value.g(), value.b());

  std::vector<OctreeNode*> nodes;

  octree->root->getAllNodesInVolumeOnSamplingDepth(nodes, minPosition, maxPosition, config.normalSamplingLevel, false);

  feature.calculateDescriptor(nodes);
  feature.cornerness3D = sure::calculateCornerness(nodes);
}