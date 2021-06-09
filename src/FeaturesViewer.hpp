#pragma once

#include <MFeatures.hpp>
#include <MSfMData.hpp>
#include <MTracks.hpp>

#include <QQuickItem>

namespace qtAliceVision {

  /**
   * @brief Display extracted features / Matches / Tracks
   */
  class FeaturesViewer : public QQuickItem
  {
    Q_OBJECT

      /// Display properties

      // Display all the 2D features extracted from the image
      Q_PROPERTY(bool displayFeatures MEMBER _displayFeatures NOTIFY displayFeaturesChanged)
      // Display the of the center of the tracks over time
      Q_PROPERTY(bool displayTracks MEMBER _displayTracks NOTIFY displayTracksChanged)
      // Display the center of the tracks unvalidated after resection
      Q_PROPERTY(bool displayMatches MEMBER _displayMatches NOTIFY displayMatchesChanged)
      // Display the 3D reprojection of the features associated to a landmark
      Q_PROPERTY(bool displayLandmarks MEMBER _displayLandmarks NOTIFY displayLandmarksChanged)
      // Feature display mode (see FeatureDisplayMode enum)
      Q_PROPERTY(FeatureDisplayMode featureDisplayMode MEMBER _featureDisplayMode NOTIFY featureDisplayModeChanged)
      // Track display mode (see TrackDisplayMode enum)
      Q_PROPERTY(TrackDisplayMode trackDisplayMode MEMBER _trackDisplayMode NOTIFY trackDisplayModeChanged)
      // Display only contiguous tracks
      Q_PROPERTY(bool trackContiguousFilter MEMBER _trackContiguousFilter NOTIFY trackContiguousFilterChanged)
      // Display only tracks with at least one inlier
      Q_PROPERTY(bool trackInliersFilter MEMBER _trackInliersFilter NOTIFY trackInliersFilterChanged)
      // Minimum track feature scale to display
      Q_PROPERTY(float trackMinFeatureScaleFilter MEMBER _trackMinFeatureScaleFilter NOTIFY trackMinFeatureScaleFilterChanged)
      // Minimum track feature scale to display
      Q_PROPERTY(float trackMaxFeatureScaleFilter MEMBER _trackMaxFeatureScaleFilter NOTIFY trackMaxFeatureScaleFilterChanged)
      // Features color
      Q_PROPERTY(QColor featureColor MEMBER _featureColor NOTIFY featureColorChanged)
      // Matches color
      Q_PROPERTY(QColor matchColor MEMBER _matchColor NOTIFY matchColorChanged)
      // Landmarks color
      Q_PROPERTY(QColor landmarkColor MEMBER _landmarkColor NOTIFY landmarkColorChanged)

      /// Data properties

      // Describer type
      Q_PROPERTY(QString describerType MEMBER _describerType NOTIFY describerTypeChanged)
      // Pointer to Features
      Q_PROPERTY(qtAliceVision::MFeatures* mfeatures READ getMFeatures WRITE setMFeatures NOTIFY featuresChanged)

  public:

    /// Helpers

    enum FeatureDisplayMode {
      Points = 0,         // Simple points (GL_POINTS)
      Squares,            // Scaled filled squares (GL_TRIANGLES)
      OrientedSquares     // Scaled and oriented squares (GL_LINES)
    };
    Q_ENUM(FeatureDisplayMode)

    enum TrackDisplayMode {
      LinesOnly = 0,
      WithCurrentMatches,
      WithAllMatches
    };
    Q_ENUM(TrackDisplayMode)

    /// Signals

    Q_SIGNAL void displayFeaturesChanged();
    Q_SIGNAL void displayTracksChanged();
    Q_SIGNAL void displayMatchesChanged();
    Q_SIGNAL void displayLandmarksChanged();

    Q_SIGNAL void featureDisplayModeChanged();
    Q_SIGNAL void trackDisplayModeChanged();

    Q_SIGNAL void trackContiguousFilterChanged();
    Q_SIGNAL void trackInliersFilterChanged();

    Q_SIGNAL void trackMinFeatureScaleFilterChanged();
    Q_SIGNAL void trackMaxFeatureScaleFilterChanged();

    Q_SIGNAL void featureColorChanged();
    Q_SIGNAL void matchColorChanged();
    Q_SIGNAL void landmarkColorChanged();

    Q_SIGNAL void describerTypeChanged();
    Q_SIGNAL void featuresChanged();

    /// Public methods

    explicit FeaturesViewer(QQuickItem* parent = nullptr);
    ~FeaturesViewer() override;

    MFeatures* getMFeatures() { return _mfeatures; }
    void setMFeatures(MFeatures* sfmData);

  private:
    /// Custom QSGNode update
    QSGNode* updatePaintNode(QSGNode* oldNode, QQuickItem::UpdatePaintNodeData* data) override;

  private:
    void updatePaintFeatures(QSGNode* oldNode, QSGNode* node);
    void updatePaintTracks(QSGNode* oldNode, QSGNode* node);
    void updatePaintMatches(QSGNode* oldNode, QSGNode* node);
    void updatePaintLandmarks(QSGNode* oldNode, QSGNode* node);

    bool _displayFeatures = true;
    bool _displayTracks = true;
    bool _displayMatches = true;
    bool _displayLandmarks = true;

    FeatureDisplayMode _featureDisplayMode = FeaturesViewer::Points;
    TrackDisplayMode _trackDisplayMode = FeaturesViewer::WithCurrentMatches;
    
    bool _trackContiguousFilter = true;
    bool _trackInliersFilter = false;

    float _trackMinFeatureScaleFilter = 0.f;
    float _trackMaxFeatureScaleFilter = 1.f;

    QColor _featureColor = QColor(20, 220, 80);
    QColor _matchColor = QColor(255, 127, 0);
    QColor _landmarkColor = QColor(255, 0, 0);

    QString _describerType = "sift";
    MFeatures* _mfeatures = nullptr;
  };

} // namespace qtAliceVision
