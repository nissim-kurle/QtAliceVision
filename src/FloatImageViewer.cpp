#include "FloatImageViewer.hpp"
#include "FloatTexture.hpp"

#include <QSGGeometry>
#include <QSGMaterial>
#include <QSGMaterialShader>
#include <QSGFlatColorMaterial>
#include <QSGTexture>
#include <QThreadPool>

#include <aliceVision/image/resampling.hpp>
#include <aliceVision/image/io.hpp>
#include <aliceVision/image/Image.hpp>
#include <aliceVision/image/convertion.hpp>

namespace qtAliceVision
{

FloatImageIORunnable::FloatImageIORunnable(const QUrl& path, int downscaleLevel, QObject* parent)
    : QObject(parent), _path(path), _downscaleLevel(downscaleLevel)
{}

void FloatImageIORunnable::run()
{
    using namespace aliceVision;
    QSharedPointer<FloatImage> result;
    QSize sourceSize(0, 0);
    QVariantMap qmetadata;

    try
    {
        const auto path = _path.toLocalFile().toUtf8().toStdString();
        FloatImage image;
        image::readImage(path, image, image::EImageColorSpace::LINEAR);  // linear: sRGB conversion is done in display shader

        sourceSize = QSize(image.Width(), image.Height());

        // ensure it fits in GPU memory
        if (FloatTexture::maxTextureSize() != -1)
        {
            const auto maxTextureSize = FloatTexture::maxTextureSize();
            while (maxTextureSize != -1 &&
                (image.Width() > maxTextureSize || image.Height() > maxTextureSize))
            {
                FloatImage tmp;
                aliceVision::image::ImageHalfSample(image, tmp);
                image = std::move(tmp);
            }
        }
        
        // ensure it fits in RAM memory
        for (size_t i = 0; i < _downscaleLevel; i++)
        {
            FloatImage tmp;
            aliceVision::image::ImageHalfSample(image, tmp);
            image = std::move(tmp);
        }
        
        // load metadata as well
        const auto metadata = image::readImageMetadata(path);
        for (const auto& item : metadata)
        {
            qmetadata[QString::fromStdString(item.name().string())] = QString::fromStdString(item.get_string());
        }

        auto sharedImage = std::make_unique<FloatImage>();
        sharedImage->swap(image);
        result = QSharedPointer<FloatImage>(sharedImage.release());
    }
    catch (std::exception& e)
    {
        qInfo() << "[QtAliceVision] Failed to load image: " << _path
            << "\n" << e.what();
    }

    Q_EMIT resultReady(result, sourceSize, qmetadata);
}

namespace
{
    class FloatImageViewerMaterialShader;

    class FloatImageViewerMaterial : public QSGMaterial
    {
    public:
        FloatImageViewerMaterial()
        {

        }

        QSGMaterialType* type() const override
        {
            static QSGMaterialType type;
            return &type;
        }

        int compare(const QSGMaterial* other) const override
        {
            Q_ASSERT(other && type() == other->type());
            return other == this ? 0 : (other > this ? 1 : -1);
        }

        QSGMaterialShader* createShader(QSGRendererInterface::RenderMode) const override;

        struct
        {
            // warning: matches layout and padding of FloatImageViewer.vert/frag shaders
            QVector4D channelOrder = QVector4D(0, 1, 2, 3);
            QVector2D fisheyeCircleCoord = QVector2D(0, 0);
            float gamma = 1.f;
            float gain = 0.f;
            float fisheyeCircleRadius = 0.f;
            float aspectRatio = 0.f;
        } uniforms;

        bool dirtyUniforms;
        std::unique_ptr<FloatTexture> texture;
    };

    class FloatImageViewerMaterialShader : public QSGMaterialShader
    {
    public:
        FloatImageViewerMaterialShader()
        {
            setShaderFileName(VertexStage, QLatin1String(":/shaders/FloatImageViewer.vert.qsb"));
            setShaderFileName(FragmentStage, QLatin1String(":/shaders/FloatImageViewer.frag.qsb"));
        }

        bool updateUniformData(RenderState& state, QSGMaterial* newMaterial, QSGMaterial* oldMaterial) override
        {
            bool changed = false;
            QByteArray* buf = state.uniformData();
            Q_ASSERT(buf->size() >= 84);
            if (state.isMatrixDirty())
            {
                const QMatrix4x4 m = state.combinedMatrix();
                memcpy(buf->data() + 0, m.constData(), 64);
                changed = true;
            }
            if (state.isOpacityDirty()) {
                const float opacity = state.opacity();
                memcpy(buf->data() + 64, &opacity, 4);
                changed = true;
            }
            auto* customMaterial = static_cast<FloatImageViewerMaterial*>(newMaterial);
            if (oldMaterial != newMaterial || customMaterial->dirtyUniforms) {
                memcpy(buf->data() + 80, &customMaterial->uniforms, 40);
                customMaterial->dirtyUniforms = false;
                changed = true;
            }
            return changed;
        }

        void updateSampledImage(RenderState& state, int binding, QSGTexture** texture, QSGMaterial* newMaterial, QSGMaterial*) override
        {
            FloatImageViewerMaterial* mat = static_cast<FloatImageViewerMaterial*>(newMaterial);
            if (binding == 1)
            {
                mat->texture->commitTextureOperations(state.rhi(), state.resourceUpdateBatch());
                *texture = mat->texture.get();
            }
        }
    };

    QSGMaterialShader* FloatImageViewerMaterial::createShader(QSGRendererInterface::RenderMode) const
    {
        return new FloatImageViewerMaterialShader;
    }

    class FloatImageViewerNode : public QSGGeometryNode
    {
    public:
        FloatImageViewerNode(int vertexCount, int indexCount)
        {
            auto* m = new FloatImageViewerMaterial;
            setMaterial(m);
            setFlag(OwnsMaterial, true);

            QSGGeometry* geometry = new QSGGeometry(QSGGeometry::defaultAttributes_TexturedPoint2D(), vertexCount, indexCount);
            QSGGeometry::updateTexturedRectGeometry(geometry, QRect(), QRect());
            geometry->setDrawingMode(GL_TRIANGLES);
            geometry->setIndexDataPattern(QSGGeometry::StaticPattern);
            geometry->setVertexDataPattern(QSGGeometry::StaticPattern);
            setGeometry(geometry);
            setFlag(OwnsGeometry, true);

            {
                /* Geometry and Material for the Grid */
                _gridNode = new QSGGeometryNode;
                auto gridMaterial = new QSGFlatColorMaterial;
                {
                    // Vertexcount of the grid is equal to indexCount of the image
                    QSGGeometry* geometryLine = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), indexCount);
                    geometryLine->setDrawingMode(GL_LINES);
                    geometryLine->setLineWidth(2);

                    _gridNode->setGeometry(geometryLine);
                    _gridNode->setFlags(QSGNode::OwnsGeometry);
                    _gridNode->setMaterial(gridMaterial);
                    _gridNode->setFlags(QSGNode::OwnsMaterial);
                }
                appendChildNode(_gridNode);
            }
        }

        void setSubdivisions(int vertexCount, int indexCount)
        {
            geometry()->allocate(vertexCount, indexCount);
            markDirty(QSGNode::DirtyGeometry);

            // Vertexcount of the grid is equal to indexCount of the image
            _gridNode->geometry()->allocate(indexCount);
            _gridNode->markDirty(QSGNode::DirtyGeometry);
        }

        void updatePaintSurface(Surface & surface, QSize textureSize, int downscaleLevel, bool canBeHovered)
        {
            // Highlight
            if (canBeHovered)
            {
                if (surface.getMouseOver())
                {
                    auto* m = static_cast<FloatImageViewerMaterial*>(material());
                    setGamma(m->uniforms.gamma + 1.f);
                }
                markDirty(QSGNode::DirtyMaterial);
            }

            // If vertices has changed, Re-Compute the grid 
            if (surface.hasVerticesChanged())
            {
                // Retrieve Vertices and Index Data
                QSGGeometry::TexturedPoint2D* vertices = geometry()->vertexDataAsTexturedPoint2D();
                quint16* indices = geometry()->indexDataAsUShort();

                // Update surface
                surface.update(vertices, indices, textureSize, downscaleLevel);

                geometry()->markIndexDataDirty();
                geometry()->markVertexDataDirty();
                markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);

                // Fill the Surface vertices array
                surface.fillVertices(vertices);
            }

            // Draw the grid if Distortion Viewer is enabled and Grid Mode is enabled
            surface.getDisplayGrid() ? surface.computeGrid(_gridNode->geometry()) : surface.removeGrid(_gridNode->geometry());
            _gridNode->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);
        }

        void setRect(const QRectF& bounds)
        {
            QSGGeometry::updateTexturedRectGeometry(geometry(), bounds, QRectF(0, 0, 1, 1));
            markDirty(QSGNode::DirtyGeometry);
        }

        void setChannelOrder(QVector4D channelOrder)
        {
            auto* m = static_cast<FloatImageViewerMaterial*>(material());
            m->uniforms.channelOrder = channelOrder;
            m->dirtyUniforms = true;
            markDirty(DirtyMaterial);
        }

        void setBlending(bool value)
        {
            auto* m = static_cast<FloatImageViewerMaterial*>(material());
            m->setFlag(QSGMaterial::Blending, value);
        }

        void setGamma(float gamma)
        {
            auto* m = static_cast<FloatImageViewerMaterial*>(material());
            m->uniforms.gamma = gamma;
            m->dirtyUniforms = true;
            markDirty(DirtyMaterial);
        }

        void setGain(float gain)
        {
            auto* m = static_cast<FloatImageViewerMaterial*>(material());
            m->uniforms.gain = gain;
            m->dirtyUniforms = true;
            markDirty(DirtyMaterial);
        }

        void setTexture(std::unique_ptr<FloatTexture> texture)
        {
            auto* m = static_cast<FloatImageViewerMaterial*>(material());
            m->texture = std::move(texture);
            markDirty(DirtyMaterial);
        }

        void setGridColor(const QColor & gridColor)
        {
            auto* m = static_cast<QSGFlatColorMaterial*>(_gridNode->material());
            m->setColor(gridColor);
        }

        void setFisheye(float aspectRatio, float fisheyeCircleRadius, QVector2D fisheyeCircleCoord)
        {
            auto* m = static_cast<FloatImageViewerMaterial*>(_gridNode->material());
            m->uniforms.aspectRatio = aspectRatio;
            m->uniforms.fisheyeCircleRadius = fisheyeCircleRadius;
            m->uniforms.fisheyeCircleCoord = fisheyeCircleCoord;
            m->dirtyUniforms = true;
            markDirty(DirtyMaterial);
        }

        void resetFisheye()
        {
            auto* m = static_cast<FloatImageViewerMaterial*>(_gridNode->material());
            m->uniforms.fisheyeCircleRadius = 0.f;
            m->dirtyUniforms = true;
            markDirty(DirtyMaterial);
        }

    private:
        QSGGeometryNode * _gridNode;
    };
}

FloatImageViewer::FloatImageViewer(QQuickItem* parent)
    : QQuickItem(parent)
{
    setFlag(QQuickItem::ItemHasContents, true);

    // CONNECTS
    connect(this, &FloatImageViewer::gammaChanged, this, [this] { _gammaChanged = true; update(); });
    connect(this, &FloatImageViewer::gainChanged, this, [this] { _gainChanged = true; update(); });
    
    connect(this, &FloatImageViewer::textureSizeChanged, this, &FloatImageViewer::update);
    connect(this, &FloatImageViewer::sourceSizeChanged, this, &FloatImageViewer::update);
    connect(this, &FloatImageViewer::channelModeChanged, this, [this] { _channelModeChanged = true; update(); });
    connect(this, &FloatImageViewer::imageChanged, this, &FloatImageViewer::update);
    connect(this, &FloatImageViewer::sourceChanged, this, &FloatImageViewer::reload);
    
    connect(this, &FloatImageViewer::channelModeChanged, this, &FloatImageViewer::update);
 
    connect(this, &FloatImageViewer::downscaleLevelChanged, this, &FloatImageViewer::reload);

    connect(&_surface, &Surface::gridColorChanged, this, &FloatImageViewer::update);
    connect(&_surface, &Surface::gridOpacityChanged, this, &FloatImageViewer::update);
    connect(&_surface, &Surface::displayGridChanged, this, &FloatImageViewer::update);
   
    connect(&_surface, &Surface::mouseOverChanged, this, &FloatImageViewer::update);
    connect(&_surface, &Surface::viewerTypeChanged, this, &FloatImageViewer::update);

    connect(&_surface, &Surface::subdivisionsChanged, this, &FloatImageViewer::update);
    connect(&_surface, &Surface::verticesChanged, this, &FloatImageViewer::update);
}

FloatImageViewer::~FloatImageViewer()
{
}

// LOADING FUNCTIONS
void FloatImageViewer::setLoading(bool loading)
{
    if (_loading == loading)
    {
        return;
    }
    _loading = loading;
    Q_EMIT loadingChanged();
}

void FloatImageViewer::reload()
{
    if (_clearBeforeLoad)
    {
        _image.reset();
        _imageChanged = true;
        Q_EMIT imageChanged();
    }
    _outdated = false;

    if (!_source.isValid())
    {
        if (_loading) _outdated = true;
        _image.reset();
        _imageChanged = true;
        _surface.clearVertices();
        _surface.verticesChanged();
        Q_EMIT imageChanged();
        return;
    }

    if (!_loading)
    {
        setLoading(true);

        // async load from file
        auto ioRunnable = new FloatImageIORunnable(_source, _downscaleLevel);
        connect(ioRunnable, &FloatImageIORunnable::resultReady, this, &FloatImageViewer::onResultReady);
        QThreadPool::globalInstance()->start(ioRunnable);
    }
    else
    {
        _outdated = true;
    }
}

void FloatImageViewer::onResultReady(QSharedPointer<FloatImage> image, QSize sourceSize, const QVariantMap& metadata)
{
    setLoading(false);

    if (_outdated)
    {
        // another request has been made while io thread was working
        _image.reset();
        reload();
        return;
    }

    _surface.setVerticesChanged(true);
    _surface.setNeedToUseIntrinsic(true);
    _image = image;
    _imageChanged = true;
    Q_EMIT imageChanged();

    _sourceSize = sourceSize;
    Q_EMIT sourceSizeChanged();

    _metadata = metadata;
    Q_EMIT metadataChanged();
}

void FloatImageViewer::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    _geometryChanged = true;
}

QVector4D FloatImageViewer::pixelValueAt(int x, int y)
{
    if (!_image)
    {
        // qInfo() << "[QtAliceVision] FloatImageViewer::pixelValueAt(" << x << ", " << y << ") => no valid image";
        return QVector4D(0.0, 0.0, 0.0, 0.0);
    }
    else if (x < 0 || x >= _image->Width() ||
        y < 0 || y >= _image->Height())
    {
        // qInfo() << "[QtAliceVision] FloatImageViewer::pixelValueAt(" << x << ", " << y << ") => out of range";
        return QVector4D(0.0, 0.0, 0.0, 0.0);
    }
    aliceVision::image::RGBAfColor color = (*_image)(y, x);
    // qInfo() << "[QtAliceVision] FloatImageViewer::pixelValueAt(" << x << ", " << y << ") => valid pixel: " << color(0) << ", " << color(1) << ", " << color(2) << ", " << color(3);
    return QVector4D(color(0), color(1), color(2), color(3));
}

QSGNode* FloatImageViewer::updatePaintNode(QSGNode* oldNode, QQuickItem::UpdatePaintNodeData* data)
{
    auto* node = static_cast<FloatImageViewerNode*>(oldNode);
    bool isNewNode = false;
    if (!node)
    {
        node = new FloatImageViewerNode(_surface.vertexCount(), _surface.indexCount());
        isNewNode = true;
    }
    else if (_surface.hasSubdivisionsChanged())
    {
        node->setSubdivisions(_surface.vertexCount(), _surface.indexCount());
    }

    node->setGridColor(_surface.getGridColor());

    if (_imageChanged)
    {
        QSize newTextureSize;
        auto texture = std::make_unique<FloatTexture>();
        if (_image)
        {
            texture->setImage(_image);
            texture->setFiltering(QSGTexture::Nearest);
            texture->setHorizontalWrapMode(QSGTexture::Repeat);
            texture->setVerticalWrapMode(QSGTexture::Repeat);
            newTextureSize = texture->textureSize();

            //Crop the image to only display what is inside the fisheye circle
            const aliceVision::camera::EquiDistant* intrinsicEquiDistant = _surface.getIntrinsicEquiDistant();
            if (_cropFisheye && intrinsicEquiDistant)
            {
                const aliceVision::Vec3 fisheyeCircleParams(intrinsicEquiDistant->getCircleCenterX(), intrinsicEquiDistant->getCircleCenterY(), intrinsicEquiDistant->getCircleRadius());

                const double width = _image->Width() * pow(2.0, _downscaleLevel);
                const double height = _image->Height() * pow(2.0, _downscaleLevel);
                const double aspectRatio = (width > height) ? width / height : height / width;

                const double radiusInPercentage = (fisheyeCircleParams.z() / ((width > height) ? height : width)) * 2.0;

                //Radius is converted in uv coordinates (0, 0.5)
                const double radius = 0.5 * (radiusInPercentage);

                node->setFisheye(aspectRatio, radius, QVector2D(fisheyeCircleParams.x() / width, fisheyeCircleParams.y() / height));
            }
            else
            {
                node->resetFisheye();
            }
        }
        node->setTexture(std::move(texture));

        if (_textureSize != newTextureSize)
        {
            _textureSize = newTextureSize;
            _geometryChanged = true;
            Q_EMIT textureSizeChanged();
        }
    }
    _imageChanged = false;

    const auto newBoundingRect = boundingRect();
    if (_geometryChanged || _boundingRect != newBoundingRect)
    {
        _boundingRect = newBoundingRect;

        const float windowRatio = _boundingRect.width() / _boundingRect.height();
        const float textureRatio = _textureSize.width() / float(_textureSize.height());
        QRectF geometryRect = _boundingRect;
        if (windowRatio > textureRatio)
        {
            geometryRect.setWidth(geometryRect.height() * textureRatio);
        }
        else
        {
            geometryRect.setHeight(geometryRect.width() / textureRatio);
        }
        geometryRect.moveCenter(_boundingRect.center());

        static const int MARGIN = 0;
        geometryRect = geometryRect.adjusted(MARGIN, MARGIN, -MARGIN, -MARGIN);

        QSGGeometry::updateTexturedRectGeometry(node->geometry(), geometryRect, QRectF(0, 0, 1, 1));
        node->markDirty(QSGNode::DirtyGeometry);
    }
    _geometryChanged = false;

    if (isNewNode || _gammaChanged)
    {
        node->setGamma(_gamma);
    }
    _gammaChanged = false;

    if (isNewNode || _gainChanged)
    {
        node->setGain(_gain);
    }
    _gainChanged = false;

    if (isNewNode || _channelModeChanged)
    {
        QVector4D channelOrder(0.f, 1.f, 2.f, 3.f);
        switch (_channelMode)
        {
        case EChannelMode::R:
            channelOrder = QVector4D(0.f, 0.f, 0.f, -1.f);
            break;
        case EChannelMode::G:
            channelOrder = QVector4D(1.f, 1.f, 1.f, -1.f);
            break;
        case EChannelMode::B:
            channelOrder = QVector4D(2.f, 2.f, 2.f, -1.f);
            break;
        case EChannelMode::A:
            channelOrder = QVector4D(3.f, 3.f, 3.f, -1.f);
            break;
        }
        node->setChannelOrder(channelOrder);
        node->setBlending(_channelMode == EChannelMode::RGBA);
    }
    _channelModeChanged = false;

    if (!isNewNode && _image)
    {
        node->updatePaintSurface(_surface, _textureSize, _downscaleLevel, _canBeHovered);
    }
    return node;
}

}
