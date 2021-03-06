#include <stdlib.h>
#include <memory>
#include <chrono>
#include <future>

#include <GL/glew.h>
#include <GL/freeglut.h>

#include <SkImageDecoder.h>

#include <OsmAndCore/QtExtensions.h>
#include <QString>
#include <QList>
#include <QFile>
#include <QMutex>
#include <QElapsedTimer>

#include <OsmAndCore.h>
#include <OsmAndCore/Concurrent.h>
#include <OsmAndCore/Common.h>
#include <OsmAndCore/Utilities.h>
#include <OsmAndCore/Logging.h>
#include <OsmAndCore/ResourcesManager.h>
#include <OsmAndCore/ObfsCollection.h>
#include <OsmAndCore/ObfDataInterface.h>
#include <OsmAndCore/WorldRegions.h>
#include <OsmAndCore/Map/Rasterizer.h>
#include <OsmAndCore/Map/MapPresentationEnvironment.h>
#include <OsmAndCore/Map/IMapStylesCollection.h>
#include <OsmAndCore/Map/MapStylesCollection.h>
#include <OsmAndCore/Map/IMapStylesPresetsCollection.h>
#include <OsmAndCore/Map/MapStyleEvaluator.h>
#include <OsmAndCore/Map/IMapRenderer.h>
#include <OsmAndCore/Map/OnlineRasterMapTileProvider.h>
#include <OsmAndCore/Map/OnlineTileSources.h>
#include <OsmAndCore/Map/HillshadeTileProvider.h>
#include <OsmAndCore/Map/IMapElevationDataProvider.h>
#include <OsmAndCore/Map/HeightmapTileProvider.h>
#include <OsmAndCore/Map/BinaryMapDataProvider.h>
#include <OsmAndCore/Map/BinaryMapPrimitivesProvider.h>
#include <OsmAndCore/Map/BinaryMapRasterBitmapTileProvider_Software.h>
#include <OsmAndCore/Map/BinaryMapRasterBitmapTileProvider_GPU.h>
#include <OsmAndCore/Map/BinaryMapDataMetricsBitmapTileProvider.h>
#include <OsmAndCore/Map/BinaryMapPrimitivesProvider.h>
#include <OsmAndCore/Map/BinaryMapPrimitivesMetricsBitmapTileProvider.h>
#include <OsmAndCore/Map/BinaryMapStaticSymbolsProvider.h>
#include <OsmAndCore/Map/MapMarkersCollection.h>
#include <OsmAndCore/Map/MapMarkerBuilder.h>
#include <OsmAndCore/Map/MapMarker.h>
#include <OsmAndCore/Map/MapAnimator.h>
#include <OsmAndCore/FavoriteLocationsGpxCollection.h>
#include <OsmAndCore/Map/FavoriteLocationsPresenter.h>

bool glutWasInitialized = false;
QMutex glutWasInitializedFlagMutex;

OsmAnd::AreaI viewport;
std::shared_ptr<OsmAnd::IMapRenderer> renderer;
std::shared_ptr<OsmAnd::ResourcesManager> resourcesManager;
std::shared_ptr<const OsmAnd::IObfsCollection> obfsCollection;
std::shared_ptr<OsmAnd::BinaryMapDataProvider> binaryMapDataProvider;
std::shared_ptr<OsmAnd::MapPresentationEnvironment> mapPresentationEnvironment;
std::shared_ptr<OsmAnd::Primitiviser> primitivizer;
std::shared_ptr<OsmAnd::BinaryMapPrimitivesProvider> binaryMapPrimitivesProvider;
std::shared_ptr<const OsmAnd::IMapStylesCollection> stylesCollection;
std::shared_ptr<const OsmAnd::MapStyle> style;
std::shared_ptr<OsmAnd::MapAnimator> animator;
std::shared_ptr<OsmAnd::BinaryMapStaticSymbolsProvider> binaryMapStaticSymbolProvider;
std::shared_ptr<OsmAnd::MapMarkersCollection> markers;
std::shared_ptr<OsmAnd::FavoriteLocationsGpxCollection> favorites;
std::shared_ptr<OsmAnd::FavoriteLocationsPresenter> favoritesPresenter;
std::shared_ptr<OsmAnd::MapMarker> lastClickedLocationMarker;

bool obfsDirSpecified = false;
QDir obfsDir;
bool dataDirSpecified = false;
QDir dataDir;
QDir cacheDir(QDir::current());
QDir heightsDir;
bool wasHeightsDirSpecified = false;
QFileInfoList styleFiles;
QString styleName = "default";

#define USE_GREEN_TEXT_COLOR 1

#if defined(WIN32)
const bool useGpuWorker = true;
#else
const bool useGpuWorker = false;
#endif
bool use43 = false;
bool constantRefresh = false;
bool nSight = false;
bool gDEBugger = false;
const float density = 1.0f;

bool renderWireframe = false;
void reshapeHandler(int newWidth, int newHeight);
void mouseHandler(int button, int state, int x, int y);
void mouseMotion(int x, int y);
void mouseWheelHandler(int button, int dir, int x, int y);
void keyboardHandler(unsigned char key, int x, int y);
void specialHandler(int key, int x, int y);
void displayHandler(void);
void idleHandler(void);
void closeHandler(void);
void activateProvider(OsmAnd::RasterMapLayerId layerId, int idx);
void verifyOpenGL();

OsmAnd::PointI lastClickedLocation31;

int main(int argc, char** argv)
{
    //////////////////////////////////////////////////////////////////////////
    OsmAnd::InitializeCore();

    const std::unique_ptr<SkImageDecoder> pngDecoder(CreatePNGImageDecoder());

    for (int argIdx = 1; argIdx < argc; argIdx++)
    {
        std::cout << "Arg: " << argv[argIdx] << std::endl;

        const QString arg(argv[argIdx]);

        if (arg.startsWith("-stylesPath="))
        {
            auto path = arg.mid(strlen("-stylesPath="));
            QDir dir(path);
            if (!dir.exists())
            {
                std::cerr << "Style directory '" << path.toStdString() << "' does not exist" << std::endl;
                OsmAnd::ReleaseCore();
                return EXIT_FAILURE;
            }

            OsmAnd::Utilities::findFiles(dir, QStringList() << "*.render.xml", styleFiles);
        }
        else if (arg.startsWith("-style="))
        {
            styleName = arg.mid(strlen("-style="));
        }
        else if (arg.startsWith("-obfsDir="))
        {
            auto obfsDirPath = arg.mid(strlen("-obfsDir="));
            obfsDir = QDir(obfsDirPath);
            obfsDirSpecified = true;
        }
        else if (arg.startsWith("-dataDir="))
        {
            auto dataDirPath = arg.mid(strlen("-dataDir="));
            dataDir = QDir(dataDirPath);
            dataDirSpecified = true;
        }
        else if (arg.startsWith("-cacheDir="))
        {
            cacheDir = QDir(arg.mid(strlen("-cacheDir=")));
        }
        else if (arg.startsWith("-heightsDir="))
        {
            heightsDir = QDir(arg.mid(strlen("-heightsDir=")));
            wasHeightsDirSpecified = true;
        }
        else if (arg == "-nsight")
        {
            use43 = true;
            constantRefresh = true;
            nSight = true;
            gDEBugger = false;
        }
        else if (arg == "-gdebugger")
        {
            use43 = false;
            constantRefresh = true;
            nSight = false;
            gDEBugger = true;
        }
    }

    if (!obfsDirSpecified && !dataDirSpecified)
    {
        std::cerr << "Nor OBFs directory nor data directory was specified" << std::endl;
        OsmAnd::ReleaseCore();
        return EXIT_FAILURE;
    }
    if (obfsDirSpecified && !obfsDir.exists())
    {
        std::cerr << "OBFs directory does not exist" << std::endl;
        OsmAnd::ReleaseCore();
        return EXIT_FAILURE;
    }

    renderer = OsmAnd::createMapRenderer(OsmAnd::MapRendererClass::AtlasMapRenderer_OpenGL3);
    if (!renderer)
    {
        std::cout << "No supported renderer" << std::endl;
        OsmAnd::ReleaseCore();
        return EXIT_FAILURE;
    }
    animator.reset(new OsmAnd::MapAnimator());
    animator->setMapRenderer(renderer);

    markers.reset(new OsmAnd::MapMarkersCollection());
    std::shared_ptr<OsmAnd::MapMarkerBuilder> markerBuilder(new OsmAnd::MapMarkerBuilder());
    {
        std::shared_ptr<SkBitmap> locationPinImage(new SkBitmap());
        pngDecoder->DecodeFile("d:\\OpenSource\\OsmAnd\\iOS7_icons_extended\\PNG\\Maps\\location\\location-32.png", locationPinImage.get());
        markerBuilder->setPinIcon(locationPinImage);
    }
    {
        std::shared_ptr<SkBitmap> locationOnMapSurfaceImage(new SkBitmap());
        pngDecoder->DecodeFile("d:\\OpenSource\\OsmAnd\\iOS7_icons_extended\\PNG\\Camping_Equipment\\campfire\\campfire-32.png", locationOnMapSurfaceImage.get());
        markerBuilder->addOnMapSurfaceIcon(reinterpret_cast<OsmAnd::MapMarker::OnSurfaceIconKey>(1), locationOnMapSurfaceImage);
    }
    markerBuilder->setIsAccuracyCircleSupported(true);
    markerBuilder->setAccuracyCircleBaseColor(OsmAnd::FColorRGB(1.0f, 0.0f, 0.0f));
    lastClickedLocationMarker = markerBuilder->buildAndAddToCollection(markers);
    lastClickedLocationMarker->setOnMapSurfaceIconDirection(reinterpret_cast<OsmAnd::MapMarker::OnSurfaceIconKey>(1), Q_QNAN);
    lastClickedLocationMarker->setIsAccuracyCircleVisible(true);
    lastClickedLocationMarker->setAccuracyCircleRadius(20000.0);
    /*
    renderer->addSymbolProvider(markers);
    */

    favorites.reset(new OsmAnd::FavoriteLocationsGpxCollection());
    /*
    if (favorites->loadFrom(QLatin1String("d:\\OpenSource\\OsmAnd\\favorites.gpx")))
    {
        favoritesPresenter.reset(new OsmAnd::FavoriteLocationsPresenter(favorites));
        renderer->addSymbolProvider(favoritesPresenter);
    }
    */
    
    //////////////////////////////////////////////////////////////////////////
    //QHash< QString, std::shared_ptr<const OsmAnd::WorldRegions::WorldRegion> > worldRegions;
    //OsmAnd::WorldRegions("d:\\OpenSource\\OsmAnd\\OsmAnd\\resources\\countries-info\\regions.ocbf").loadWorldRegions(worldRegions);
    //////////////////////////////////////////////////////////////////////////

    if (dataDirSpecified)
    {
        resourcesManager.reset(new OsmAnd::ResourcesManager(
            dataDir.absoluteFilePath(QLatin1String("storage")),
            dataDir.absoluteFilePath(QLatin1String("storage_ext")),
            QList<QString>(),
            dataDir.absoluteFilePath(QLatin1String("World_basemap_mini_2.obf")),
            dataDir.absoluteFilePath(QLatin1String("tmp"))));

        const auto renderer_ = renderer;
        resourcesManager->localResourcesChangeObservable.attach(nullptr,
            [renderer_]
            (const OsmAnd::ResourcesManager* const resourcesManager,
            const QList<QString>& added,
            const QList<QString>& removed,
            const QList<QString>& updated)
            {
                renderer_->reloadEverything();
            });

        obfsCollection = resourcesManager->obfsCollection;
        stylesCollection = resourcesManager->mapStylesCollection;
    }
    else if (obfsDirSpecified)
    {
        const auto manualObfsCollection = new OsmAnd::ObfsCollection();
        manualObfsCollection->addDirectory(obfsDir);

        obfsCollection.reset(manualObfsCollection);

        const auto pMapStylesCollection = new OsmAnd::MapStylesCollection();
        for (auto itStyleFile = styleFiles.begin(); itStyleFile != styleFiles.end(); ++itStyleFile)
        {
            const auto& styleFile = *itStyleFile;

            if (!pMapStylesCollection->registerStyle(styleFile.absoluteFilePath()))
                std::cout << "Failed to parse metadata of '" << styleFile.fileName().toStdString() << "' or duplicate style" << std::endl;
        }
        stylesCollection.reset(pMapStylesCollection);
    }

    if (!styleName.isEmpty())
    {
        if (!stylesCollection->obtainBakedStyle(styleName, style))
        {
            std::cout << "Failed to resolve style '" << styleName.toStdString() << "'" << std::endl;
            OsmAnd::ReleaseCore();
            return EXIT_FAILURE;
        }
        //style->dump();
    }

    mapPresentationEnvironment.reset(new OsmAnd::MapPresentationEnvironment(style));
    primitivizer.reset(new OsmAnd::Primitiviser(mapPresentationEnvironment));

    //////////////////////////////////////////////////////////////////////////

    {
        QMutexLocker scopedLocker(&glutWasInitializedFlagMutex);

        assert(glutInit != nullptr);
        glutInit(&argc, argv);

        glutSetOption(GLUT_ACTION_ON_WINDOW_CLOSE, GLUT_ACTION_CONTINUE_EXECUTION);
        glutInitWindowSize(1024, 768);
        glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA | GLUT_DEPTH);
        if (!use43)
            glutInitContextVersion(3, 0);
        else
            glutInitContextVersion(4, 3);
        glutInitContextProfile(GLUT_CORE_PROFILE);
        assert(glutCreateWindow != nullptr);
        glutCreateWindow("OsmAnd Bird : 3D map render tool");

        glutReshapeFunc(&reshapeHandler);
        glutMouseFunc(&mouseHandler);
        glutMotionFunc(&mouseMotion);
        glutMouseWheelFunc(&mouseWheelHandler);
        glutKeyboardFunc(&keyboardHandler);
        glutSpecialFunc(&specialHandler);
        glutDisplayFunc(&displayHandler);
        glutIdleFunc(&idleHandler);
        glutCloseFunc(&closeHandler);
        verifyOpenGL();

        glutWasInitialized = true;
    }

    //////////////////////////////////////////////////////////////////////////
    OsmAnd::MapRendererSetupOptions rendererSetup;
    rendererSetup.frameUpdateRequestCallback =
        []
        (const OsmAnd::IMapRenderer* const mapRenderer)
        {
            //QMutexLocker scopedLocker(&glutWasInitializedFlagMutex);

            if (glutWasInitialized)
                glutPostRedisplay();
        };
    rendererSetup.displayDensityFactor = density;
    rendererSetup.gpuWorkerThreadEnabled = useGpuWorker;
    if (rendererSetup.gpuWorkerThreadEnabled)
    {
#if defined(WIN32)
        const auto currentDC = wglGetCurrentDC();
        const auto currentContext = wglGetCurrentContext();
        const auto workerContext = wglCreateContext(currentDC);

        rendererSetup.gpuWorkerThreadEnabled = (wglShareLists(currentContext, workerContext) == TRUE);
        assert(currentContext == wglGetCurrentContext());

        rendererSetup.gpuWorkerThreadPrologue =
            [currentDC, workerContext]
        (const OsmAnd::IMapRenderer* const mapRenderer)
        {
            const auto result = (wglMakeCurrent(currentDC, workerContext) == TRUE);
            verifyOpenGL();
        };

        rendererSetup.gpuWorkerThreadEpilogue =
            []
        (const OsmAnd::IMapRenderer* const mapRenderer)
        {
            glFinish();
        };
#endif
    }
    renderer->setup(rendererSetup);
    viewport.top = 0;
    viewport.left = 0;
    viewport.bottom = 600;
    viewport.right = 800;
    renderer->setWindowSize(OsmAnd::PointI(800, 600));
    renderer->setViewport(viewport);
    /*renderer->setTarget(OsmAnd::PointI(
        OsmAnd::Utilities::get31TileNumberX(34.0062),
        OsmAnd::Utilities::get31TileNumberY(44.4039)
        ));
        renderer->setZoom(18.0f);*/
    renderer->setZoom(1.5f);
    //renderer->setAzimuth(137.6f);
    renderer->setAzimuth(69.4f);
    //renderer->setElevationAngle(35.0f);
    renderer->setElevationAngle(90.0f);
    renderer->setFogColor(OsmAnd::FColorRGB(1.0f, 1.0f, 1.0f));

    // Amsterdam
    renderer->setTarget(OsmAnd::PointI(
        1102430866,
        704978668));
    renderer->setZoom(11.0f);
    //renderer->setZoom(16.0f);
    //renderer->setZoom(4.0f);

    // Kiev
    //renderer->setTarget(OsmAnd::PointI(
    //    1255337783,
    //    724166131));
    ////renderer->setZoom(10.0f);
    //renderer->setZoom(16.0f);

    // Tokyo
    /*renderer->setTarget(OsmAnd::PointI(
        OsmAnd::Utilities::get31TileNumberX(139.6917),
        OsmAnd::Utilities::get31TileNumberY(35.689506)));
        renderer->setZoom(14.0f);*/

    // Netanya
    /*renderer->setTarget(OsmAnd::PointI(
        OsmAnd::Utilities::get31TileNumberX(34.85320),
        OsmAnd::Utilities::get31TileNumberY(32.32288)));
        renderer->setZoom(14.0f);*/

    renderer->setAzimuth(0.0f);
    //renderer->setDisplayDensityFactor(2.0f);

    auto renderConfig = renderer->configuration;
    renderConfig.heixelsPerTileSide = 32;
    renderer->setConfiguration(renderConfig);

    lastClickedLocationMarker->setPosition(renderer->state.target31);

    renderer->initializeRendering();
    //////////////////////////////////////////////////////////////////////////

    glutMainLoop();

    {
        QMutexLocker scopedLocker(&glutWasInitializedFlagMutex);

        glutWasInitialized = false;
    }

    renderer.reset();
    resourcesManager.reset();
    obfsCollection.reset();
    binaryMapDataProvider.reset();
    binaryMapPrimitivesProvider.reset();
    stylesCollection.reset();
    style.reset();
    animator.reset();

    OsmAnd::ReleaseCore();
    return EXIT_SUCCESS;
}

void reshapeHandler(int newWidth, int newHeight)
{
    viewport.right = newWidth;
    viewport.bottom = newHeight;
    renderer->setWindowSize(OsmAnd::PointI(newWidth, newHeight));
    renderer->setViewport(viewport);

    glViewport(0, 0, newWidth, newHeight);
}

bool dragInitialized = false;
int dragInitX;
int dragInitY;
OsmAnd::PointI dragInitTarget;

void mouseHandler(int button, int state, int x, int y)
{
    if (button == GLUT_LEFT_BUTTON)
    {
        if (state == GLUT_DOWN && !dragInitialized)
        {
            dragInitX = x;
            dragInitY = y;
            dragInitTarget = renderer->state.target31;

            dragInitialized = true;
        }
        else if (state == GLUT_UP && dragInitialized)
        {
            dragInitialized = false;
        }
    }
    else if (button == GLUT_RIGHT_BUTTON)
    {
        if (state == GLUT_DOWN)
        {
            renderer->getLocationFromScreenPoint(OsmAnd::PointI(x, y), lastClickedLocation31);
            lastClickedLocationMarker->setPosition(lastClickedLocation31);

            animator->pause();
            animator->cancelAllAnimations();
            //animator->animateTargetTo(lastClickedLocation31, 1.0f, OsmAnd::MapAnimator::TimingFunction::EaseInOutQuadratic);
            animator->parabolicAnimateTargetTo(lastClickedLocation31, 1.0f, OsmAnd::MapAnimator::TimingFunction::EaseInOutQuadratic, OsmAnd::MapAnimator::TimingFunction::EaseOutInQuadratic);
            animator->resume();
        }
    }
}

void mouseMotion(int x, int y)
{
    if (dragInitialized)
    {
        auto deltaX = x - dragInitX;
        auto deltaY = y - dragInitY;

        // Azimuth
        auto angle = qDegreesToRadians(renderer->state.azimuth);
        auto cosAngle = cosf(angle);
        auto sinAngle = sinf(angle);

        auto nx = deltaX * cosAngle - deltaY * sinAngle;
        auto ny = deltaX * sinAngle + deltaY * cosAngle;

        const auto tileSize31 = (1u << (31 - renderer->state.zoomBase));
        auto scale31 = static_cast<double>(tileSize31) / renderer->getScaledTileSizeOnScreen();

        OsmAnd::PointI newTarget;
        newTarget.x = dragInitTarget.x - static_cast<int32_t>(nx * scale31);
        newTarget.y = dragInitTarget.y - static_cast<int32_t>(ny * scale31);

        renderer->setTarget(newTarget);
        lastClickedLocationMarker->setPosition(newTarget);
    }
}

void mouseWheelHandler(int button, int dir, int x, int y)
{
    const auto modifiers = glutGetModifiers();
    const auto step = (modifiers & GLUT_ACTIVE_SHIFT) ? 0.1f : 0.01f;

    if (dir > 0)
    {
        renderer->setZoom(renderer->state.requestedZoom + step);
    }
    else
    {
        renderer->setZoom(renderer->state.requestedZoom - step);
    }
}

void keyboardHandler(unsigned char key, int x, int y)
{
    const auto modifiers = glutGetModifiers();
    const auto wasdZoom = static_cast<int>(renderer->state.requestedZoom);
    const auto wasdStep = (1 << (31 - wasdZoom));

    switch (key)
    {
    case '\x1B':
        glutLeaveMainLoop();
        break;
    case 'W':
    case 'w':
    {
        auto newTarget = renderer->state.target31;
        newTarget.y -= wasdStep / (key == 'w' ? 50 : 10);
        renderer->setTarget(newTarget);
    }
        break;
    case 'S':
    case 's':
    {
        auto newTarget = renderer->state.target31;
        newTarget.y += wasdStep / (key == 's' ? 50 : 10);
        renderer->setTarget(newTarget);
    }
        break;
    case 'A':
    case 'a':
    {
        auto newTarget = renderer->state.target31;
        newTarget.x -= wasdStep / (key == 'a' ? 50 : 10);
        renderer->setTarget(newTarget);
    }
        break;
    case 'D':
    case 'd':
    {
        auto newTarget = renderer->state.target31;
        newTarget.x += wasdStep / (key == 'd' ? 50 : 10);
        renderer->setTarget(newTarget);
    }
        break;
    case 'r':
        renderer->setDistanceToFog(renderer->state.fogDistance + 1.0f);
        break;
    case 'f':
        renderer->setDistanceToFog(renderer->state.fogDistance - 1.0f);
        break;
    case 'x':
        renderWireframe = !renderWireframe;
        glutPostRedisplay();
        break;
    case 'e':
    {
        if (renderer->state.elevationDataProvider)
        {
            renderer->resetElevationDataProvider();
        }
        else
        {
            if (wasHeightsDirSpecified)
            {
                //auto provider = new OsmAnd::HeightmapTileProvider(heightsDir, cacheDir.absoluteFilePath(OsmAnd::HeightmapTileProvider::defaultIndexFilename));
                //renderer->setElevationDataProvider(std::shared_ptr<OsmAnd::IMapElevationDataProvider>(provider));
            }
        }
    }
        break;
    case 't':
        renderer->setFogDensity(renderer->state.fogDensity + 0.01f);
        break;
    case 'g':
        renderer->setFogDensity(renderer->state.fogDensity - 0.01f);
        break;
    case 'u':
        renderer->setFogOriginFactor(renderer->state.fogOriginFactor + 0.01f);
        break;
    case 'j':
        renderer->setFogOriginFactor(renderer->state.fogOriginFactor - 0.01f);
        break;
    case 'i':
        renderer->setFieldOfView(renderer->state.fieldOfView + 0.5f);
        break;
    case 'k':
        renderer->setFieldOfView(renderer->state.fieldOfView - 0.5f);
        break;
    case 'o':
        renderer->setElevationDataScaleFactor(renderer->state.elevationDataScaleFactor + 0.1f);
        break;
    case 'l':
        renderer->setElevationDataScaleFactor(renderer->state.elevationDataScaleFactor - 0.1f);
        break;
    case 'c':
    {
        auto config = renderer->configuration;
        config.limitTextureColorDepthBy16bits = !config.limitTextureColorDepthBy16bits;
        renderer->setConfiguration(config);
    }
        break;
    case 'b':
    {
        auto config = renderer->configuration;
        config.texturesFilteringQuality = OsmAnd::TextureFilteringQuality::Normal;
        renderer->setConfiguration(config);
    }
        break;
    case 'n':
    {
        auto config = renderer->configuration;
        config.texturesFilteringQuality = OsmAnd::TextureFilteringQuality::Good;
        renderer->setConfiguration(config);
    }
        break;
    case 'm':
    {
        auto config = renderer->configuration;
        config.texturesFilteringQuality = OsmAnd::TextureFilteringQuality::Best;
        renderer->setConfiguration(config);
    }
        break;
    case 'p':
    {
        std::async(std::launch::async,
            [=]
        {
            const auto downloadProgress =
                []
            (const uint64_t bytesDownloaded, const uint64_t bytesTotal)
            {
                std::cout << "... downloaded " << bytesDownloaded << " of " << bytesTotal << " (" << ((bytesDownloaded * 100) / bytesTotal) << "%)" << std::endl;
            };

            resourcesManager->updateRepository();

            if (!resourcesManager->isResourceInstalled(QLatin1String("world_basemap.map.obf")))
                resourcesManager->installFromRepository(QLatin1String("world_basemap.map.obf"), downloadProgress);
            else if (resourcesManager->isInstalledResourceOutdated(QLatin1String("world_basemap.map.obf")))
                resourcesManager->updateFromRepository(QLatin1String("world_basemap.map.obf"), downloadProgress);

            if (!resourcesManager->isResourceInstalled(QLatin1String("ukraine_europe.map.obf")))
                resourcesManager->installFromRepository(QLatin1String("ukraine_europe.map.obf"), downloadProgress);
            else if (resourcesManager->isInstalledResourceOutdated(QLatin1String("ukraine_europe.map.obf")))
                resourcesManager->updateFromRepository(QLatin1String("ukraine_europe.map.obf"), downloadProgress);

            if (!resourcesManager->isResourceInstalled(QLatin1String("netherlands_europe.map.obf")))
                resourcesManager->installFromRepository(QLatin1String("netherlands_europe.map.obf"), downloadProgress);
            else if (resourcesManager->isInstalledResourceOutdated(QLatin1String("netherlands_europe.map.obf")))
                resourcesManager->updateFromRepository(QLatin1String("netherlands_europe.map.obf"), downloadProgress);
        });
    }
        break;
    case 'z':
    {
        if (renderer->state.symbolProviders.contains(binaryMapStaticSymbolProvider))
        {
            renderer->removeSymbolProvider(binaryMapStaticSymbolProvider);
            binaryMapStaticSymbolProvider.reset();
        }
        else
        {
            if (!binaryMapDataProvider)
                binaryMapDataProvider.reset(new OsmAnd::BinaryMapDataProvider(obfsCollection));
            if (!binaryMapPrimitivesProvider)
                binaryMapPrimitivesProvider.reset(new OsmAnd::BinaryMapPrimitivesProvider(binaryMapDataProvider, primitivizer));

            binaryMapStaticSymbolProvider.reset(new OsmAnd::BinaryMapStaticSymbolsProvider(binaryMapPrimitivesProvider));
            renderer->addSymbolProvider(binaryMapStaticSymbolProvider);
        }
    }
        break;
    case 'q':
        animator->pause();
        animator->cancelAllAnimations();
        animator->animateAzimuthTo(0.0f, 1.0f, OsmAnd::MapAnimator::TimingFunction::EaseInOutQuadratic);
        animator->resume();
        break;
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
    {
        auto layerId = (modifiers & GLUT_ACTIVE_ALT) ? OsmAnd::RasterMapLayerId::Overlay0 : OsmAnd::RasterMapLayerId::BaseLayer;
        activateProvider(layerId, key - '0');
    }
        break;
    case ' ':
    {
        const OsmAnd::PointI amsterdam(
            1102430866,
            704978668);
        const OsmAnd::PointI kiev(
            1255337783,
            724166131);
        static bool flag = false;
        const auto& target = flag ? amsterdam : kiev;
        flag = !flag;

        animator->pause();
        animator->cancelAllAnimations();
        animator->animateMoveTo(target, 1.0f, false, false, OsmAnd::MapAnimator::TimingFunction::EaseInOutQuadratic);
        animator->resume();
    }
        break;
    case '-':
        animator->pause();
        animator->cancelAllAnimations();
        animator->animateZoomBy(-1.0f, 1.0f, OsmAnd::MapAnimator::TimingFunction::EaseInOutQuadratic);
        animator->resume();
        break;
    case '+':
        animator->pause();
        animator->cancelAllAnimations();
        animator->animateZoomBy(+1.0f, 1.0f, OsmAnd::MapAnimator::TimingFunction::EaseInOutQuadratic);
        animator->resume();
        break;
    case '*':
        break;
    }
}

void specialHandler(int key, int x, int y)
{
    const auto modifiers = glutGetModifiers();
    const auto step = (modifiers & GLUT_ACTIVE_SHIFT) ? 1.0f : 0.1f;

    switch (key)
    {
    case GLUT_KEY_F5:
        renderer->forcedFrameInvalidate();
        break;
    case GLUT_KEY_F6:
        renderer->forcedGpuProcessingCycle();
        break;
    case GLUT_KEY_F2:
        renderer->dumpResourcesInfo();
        break;
    case GLUT_KEY_LEFT:
        renderer->setAzimuth(renderer->state.azimuth + step);
        break;
    case GLUT_KEY_RIGHT:
        renderer->setAzimuth(renderer->state.azimuth - step);
        break;
    case GLUT_KEY_UP:
        renderer->setElevationAngle(renderer->state.elevationAngle + step);
        break;
    case GLUT_KEY_DOWN:
        renderer->setElevationAngle(renderer->state.elevationAngle - step);
        break;
    }
}

void closeHandler(void)
{
    renderer->releaseRendering();
}

void activateProvider(OsmAnd::RasterMapLayerId layerId, int idx)
{
    if (idx == 0)
    {
        renderer->resetRasterLayerProvider(layerId);
    }
    else if (idx == 1)
    {
        auto tileProvider = OsmAnd::OnlineTileSources::getBuiltIn()->createProviderFor(QLatin1String("Mapnik (OsmAnd)"));
        renderer->setRasterLayerProvider(layerId, tileProvider);
    }
    else if (idx == 2)
    {
        if (!binaryMapDataProvider)
            binaryMapDataProvider.reset(new OsmAnd::BinaryMapDataProvider(obfsCollection));
        if (!binaryMapPrimitivesProvider)
            binaryMapPrimitivesProvider.reset(new OsmAnd::BinaryMapPrimitivesProvider(binaryMapDataProvider, primitivizer));

        // general
        QHash< QString, QString > settings;
        settings.insert("appMode", "browse map");
        mapPresentationEnvironment->setSettings(settings);

        auto tileProvider = new OsmAnd::BinaryMapRasterBitmapTileProvider_Software(binaryMapPrimitivesProvider);
        renderer->setRasterLayerProvider(layerId, std::shared_ptr<OsmAnd::IMapRasterBitmapTileProvider>(tileProvider));
    }
    else if (idx == 3)
    {
        if (!binaryMapDataProvider)
            binaryMapDataProvider.reset(new OsmAnd::BinaryMapDataProvider(obfsCollection));
        if (!binaryMapPrimitivesProvider)
            binaryMapPrimitivesProvider.reset(new OsmAnd::BinaryMapPrimitivesProvider(binaryMapDataProvider, primitivizer));

        // car
        QHash< QString, QString > settings;
        settings.insert("appMode", "car");
        mapPresentationEnvironment->setSettings(settings);

        auto tileProvider = new OsmAnd::BinaryMapRasterBitmapTileProvider_Software(binaryMapPrimitivesProvider);
        renderer->setRasterLayerProvider(layerId, std::shared_ptr<OsmAnd::IMapRasterBitmapTileProvider>(tileProvider));
    }
    else if (idx == 4)
    {
        if (!binaryMapDataProvider)
            binaryMapDataProvider.reset(new OsmAnd::BinaryMapDataProvider(obfsCollection));
        if (!binaryMapPrimitivesProvider)
            binaryMapPrimitivesProvider.reset(new OsmAnd::BinaryMapPrimitivesProvider(binaryMapDataProvider, primitivizer));

        // bicycle
        QHash< QString, QString > settings;
        settings.insert("appMode", "bicycle");
        mapPresentationEnvironment->setSettings(settings);

        auto tileProvider = new OsmAnd::BinaryMapRasterBitmapTileProvider_Software(binaryMapPrimitivesProvider);
        renderer->setRasterLayerProvider(layerId, std::shared_ptr<OsmAnd::IMapRasterBitmapTileProvider>(tileProvider));
    }
    else if (idx == 5)
    {
        if (!binaryMapDataProvider)
            binaryMapDataProvider.reset(new OsmAnd::BinaryMapDataProvider(obfsCollection));
        if (!binaryMapPrimitivesProvider)
            binaryMapPrimitivesProvider.reset(new OsmAnd::BinaryMapPrimitivesProvider(binaryMapDataProvider, primitivizer));

        // pedestrian
        QHash< QString, QString > settings;
        settings.insert("appMode", "pedestrian");
        mapPresentationEnvironment->setSettings(settings);

        auto tileProvider = new OsmAnd::BinaryMapRasterBitmapTileProvider_Software(binaryMapPrimitivesProvider);
        renderer->setRasterLayerProvider(layerId, std::shared_ptr<OsmAnd::IMapRasterBitmapTileProvider>(tileProvider));
    }
    else if (idx == 6)
    {
        if (!binaryMapDataProvider)
            binaryMapDataProvider.reset(new OsmAnd::BinaryMapDataProvider(obfsCollection));

        auto tileProvider = new OsmAnd::BinaryMapDataMetricsBitmapTileProvider(binaryMapDataProvider);
        renderer->setRasterLayerProvider(layerId, std::shared_ptr<OsmAnd::IMapRasterBitmapTileProvider>(tileProvider));
    }
    else if (idx == 7)
    {
        if (!binaryMapDataProvider)
            binaryMapDataProvider.reset(new OsmAnd::BinaryMapDataProvider(obfsCollection));
        if (!binaryMapPrimitivesProvider)
            binaryMapPrimitivesProvider.reset(new OsmAnd::BinaryMapPrimitivesProvider(binaryMapDataProvider, primitivizer));

        auto tileProvider = new OsmAnd::BinaryMapPrimitivesMetricsBitmapTileProvider(binaryMapPrimitivesProvider);
        renderer->setRasterLayerProvider(layerId, std::shared_ptr<OsmAnd::IMapRasterBitmapTileProvider>(tileProvider));
    }
    //else if (idx == 7)
    //{
    //    //        auto hillshadeTileProvider = new OsmAnd::HillshadeTileProvider();
    //    //        renderer->setTileProvider(layerId, hillshadeTileProvider);
    //}
}

void idleHandler(void)
{
    static std::chrono::time_point<std::chrono::high_resolution_clock> lastTimeStamp;
    static bool lastTimeStampInitialized = false;

    if (!lastTimeStampInitialized)
    {
        lastTimeStamp = std::chrono::high_resolution_clock::now();
        lastTimeStampInitialized = true;
        return;
    }

    auto currentTimeStamp = std::chrono::high_resolution_clock::now();
    std::chrono::duration<float> elapsedSeconds = currentTimeStamp - lastTimeStamp;

    animator->update(elapsedSeconds.count());

    lastTimeStamp = currentTimeStamp;
}

void displayHandler()
{
    (void)glGetError();
    glPolygonMode(GL_FRONT_AND_BACK, renderWireframe ? GL_LINE : GL_FILL);
    verifyOpenGL();
    //////////////////////////////////////////////////////////////////////////

    renderer->update();

    if (renderer->prepareFrame())
        renderer->renderFrame();
    
    verifyOpenGL();

    //////////////////////////////////////////////////////////////////////////
    if (!use43 && !nSight)
    {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glDisable(GL_DEPTH_TEST);

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        gluOrtho2D(0, viewport.width(), 0, viewport.height());

        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

        auto w = 390;
        auto h1 = 16 * 19;
        auto t = viewport.height();
        glColor4f(0.5f, 0.5f, 0.5f, 0.6f);
        glBegin(GL_QUADS);
        glVertex2f(0.0f, t);
        glVertex2f(w, t);
        glVertex2f(w, t - h1);
        glVertex2f(0.0f, t - h1);
        glEnd();
        verifyOpenGL();

#if USE_GREEN_TEXT_COLOR
        glColor3f(0.0f, 1.0f, 0.0f);
#else
        glColor3f(0.2f, 0.2f, 0.2f);
#endif
        glRasterPos2f(8, t - 16 * 1);
        glutBitmapString(GLUT_BITMAP_8_BY_13, (const unsigned char*)qPrintable(
            QString("fov (keys i,k)         : %1").arg(renderer->state.fieldOfView)));
        verifyOpenGL();

        glRasterPos2f(8, t - 16 * 2);
        glutBitmapString(GLUT_BITMAP_8_BY_13, (const unsigned char*)qPrintable(
            QString("fog distance (keys r,f): %1").arg(renderer->state.fogDistance)));
        verifyOpenGL();

        glRasterPos2f(8, t - 16 * 3);
        glutBitmapString(GLUT_BITMAP_8_BY_13, (const unsigned char*)qPrintable(
            QString("azimuth (arrows l,r)   : %1").arg(renderer->state.azimuth)));
        verifyOpenGL();

        glRasterPos2f(8, t - 16 * 4);
        glutBitmapString(GLUT_BITMAP_8_BY_13, (const unsigned char*)qPrintable(
            QString("pitch (arrows u,d)     : %1").arg(renderer->state.elevationAngle)));
        verifyOpenGL();

        glRasterPos2f(8, t - 16 * 5);
        glutBitmapString(GLUT_BITMAP_8_BY_13, (const unsigned char*)qPrintable(
            QString("target (keys w,a,s,d)  : %1 %2").arg(renderer->state.target31.x).arg(renderer->state.target31.y)));
        verifyOpenGL();

        glRasterPos2f(8, t - 16 * 6);
        glutBitmapString(GLUT_BITMAP_8_BY_13, (const unsigned char*)qPrintable(
            QString("zoom (mouse wheel)     : %1").arg(renderer->state.requestedZoom)));
        verifyOpenGL();

        glRasterPos2f(8, t - 16 * 7);
        glutBitmapString(GLUT_BITMAP_8_BY_13, (const unsigned char*)qPrintable(
            QString("zoom base              : %1").arg(renderer->state.zoomBase)));
        verifyOpenGL();

        glRasterPos2f(8, t - 16 * 8);
        glutBitmapString(GLUT_BITMAP_8_BY_13, (const unsigned char*)qPrintable(
            QString("zoom fraction          : %1").arg(renderer->state.zoomFraction)));
        verifyOpenGL();

        glRasterPos2f(8, t - 16 * 9);
        glutBitmapString(GLUT_BITMAP_8_BY_13, (const unsigned char*)qPrintable(
            QString("visible tiles          : %1").arg(renderer->getVisibleTilesCount())));
        verifyOpenGL();

        glRasterPos2f(8, t - 16 * 10);
        glutBitmapString(GLUT_BITMAP_8_BY_13, (const unsigned char*)qPrintable(
            QString("wireframe (key x)      : %1").arg(renderWireframe)));
        verifyOpenGL();

        glRasterPos2f(8, t - 16 * 11);
        glutBitmapString(GLUT_BITMAP_8_BY_13, (const unsigned char*)qPrintable(
            QString("elevation data (key e) : %1").arg((bool)renderer->state.elevationDataProvider)));
        verifyOpenGL();

        glRasterPos2f(8, t - 16 * 12);
        glutBitmapString(GLUT_BITMAP_8_BY_13, (const unsigned char*)qPrintable(
            QString("fog density (keys t,g) : %1").arg(renderer->state.fogDensity)));
        verifyOpenGL();

        glRasterPos2f(8, t - 16 * 13);
        glutBitmapString(GLUT_BITMAP_8_BY_13, (const unsigned char*)qPrintable(
            QString("fog origin F (keys u,j): %1").arg(renderer->state.fogOriginFactor)));
        verifyOpenGL();

        glRasterPos2f(8, t - 16 * 14);
        glutBitmapString(GLUT_BITMAP_8_BY_13, (const unsigned char*)qPrintable(
            QString("height scale (keys o,l): %1").arg(renderer->state.elevationDataScaleFactor)));
        verifyOpenGL();

        glRasterPos2f(8, t - 16 * 15);
        glutBitmapString(GLUT_BITMAP_8_BY_13, (const unsigned char*)qPrintable(
            QString("16-bit textures (key c): %1").arg(renderer->configuration.limitTextureColorDepthBy16bits)));
        verifyOpenGL();

        glRasterPos2f(8, t - 16 * 16);
        glutBitmapString(GLUT_BITMAP_8_BY_13, (const unsigned char*)qPrintable(
            QString("tex-filtering (b,n,m)  : %1").arg(static_cast<int>(renderer->configuration.texturesFilteringQuality))));
        verifyOpenGL();

        glRasterPos2f(8, t - 16 * 17);
        glutBitmapString(GLUT_BITMAP_8_BY_13, (const unsigned char*)qPrintable(
            QString("symbols (key z)        : %1").arg(!renderer->state.symbolProviders.isEmpty())));
        verifyOpenGL();

        glRasterPos2f(8, t - 16 * 18);
        glutBitmapString(GLUT_BITMAP_8_BY_13, (const unsigned char*)qPrintable(
            QString("symbols loaded         : %1").arg(renderer->getSymbolsCount())));
        verifyOpenGL();

        glColor4f(0.5f, 0.5f, 0.5f, 0.6f);
        glBegin(GL_QUADS);
        glVertex2f(0.0f, 16 * 13);
        glVertex2f(w, 16 * 13);
        glVertex2f(w, 0.0f);
        glVertex2f(0.0f, 0.0f);
        glEnd();
        verifyOpenGL();

#if USE_GREEN_TEXT_COLOR
        glColor3f(0.0f, 1.0f, 0.0f);
#else
        glColor3f(0.2f, 0.2f, 0.2f);
#endif
        glRasterPos2f(8, 16 * 12);
        glutBitmapString(GLUT_BITMAP_8_BY_13, (const unsigned char*)qPrintable(
            QString("Last clicked tile: (%1, %2)@%3").arg(lastClickedLocation31.x >> (31 - renderer->state.zoomBase)).arg(lastClickedLocation31.y >> (31 - renderer->state.zoomBase)).arg(renderer->state.zoomBase)));
        verifyOpenGL();

        glRasterPos2f(8, 16 * 11);
        glutBitmapString(GLUT_BITMAP_8_BY_13, (const unsigned char*)qPrintable(
            QString("Last clicked location: %1 %2").arg(lastClickedLocation31.x).arg(lastClickedLocation31.y)));
        verifyOpenGL();

        glRasterPos2f(8, 16 * 10);
        glutBitmapString(GLUT_BITMAP_8_BY_13, (const unsigned char*)qPrintable(
            QString("Tile providers (holding alt controls overlay0):")));
        verifyOpenGL();

        glRasterPos2f(8, 16 * 9);
        glutBitmapString(GLUT_BITMAP_8_BY_13, (const unsigned char*)qPrintable(
            QString("0 - disable")));
        verifyOpenGL();

        glRasterPos2f(8, 16 * 8);
        glutBitmapString(GLUT_BITMAP_8_BY_13, (const unsigned char*)qPrintable(
            QString("1 - Mapnik (OsmAnd)")));
        verifyOpenGL();

        glRasterPos2f(8, 16 * 7);
        glutBitmapString(GLUT_BITMAP_8_BY_13, (const unsigned char*)qPrintable(
            QString("2 - Offline maps [General]")));
        verifyOpenGL();

        glRasterPos2f(8, 16 * 6);
        glutBitmapString(GLUT_BITMAP_8_BY_13, (const unsigned char*)qPrintable(
            QString("3 - Offline maps [Car]")));
        verifyOpenGL();

        glRasterPos2f(8, 16 * 5);
        glutBitmapString(GLUT_BITMAP_8_BY_13, (const unsigned char*)qPrintable(
            QString("4 - Offline maps [Bicycle]")));
        verifyOpenGL();

        glRasterPos2f(8, 16 * 4);
        glutBitmapString(GLUT_BITMAP_8_BY_13, (const unsigned char*)qPrintable(
            QString("5 - Offline maps [Pedestrian]")));
        verifyOpenGL();

        glRasterPos2f(8, 16 * 3);
        glutBitmapString(GLUT_BITMAP_8_BY_13, (const unsigned char*)qPrintable(
            QString("6 - Metrics [Binary Map Data Provider]")));
        verifyOpenGL();

        glRasterPos2f(8, 16 * 2);
        glutBitmapString(GLUT_BITMAP_8_BY_13, (const unsigned char*)qPrintable(
            QString("7 - Metrics [Binary Map Primitives Provider]")));
        verifyOpenGL();

        glRasterPos2f(8, 16 * 1);
        glutBitmapString(GLUT_BITMAP_8_BY_13, (const unsigned char*)qPrintable(
            QString("8 - Metrics [Binary Map Raster Tile Provider]")));
        verifyOpenGL();
    }

    glFlush();
    glutSwapBuffers();

    if (nSight || gDEBugger || constantRefresh)
        glutPostRedisplay();
    if (gDEBugger)
        glFrameTerminatorGREMEDY();
}

void verifyOpenGL()
{
    auto result = glGetError();
    if (result == GL_NO_ERROR)
        return;

    OsmAnd::LogPrintf(OsmAnd::LogSeverityLevel::Error, "Host OpenGL error 0x%08x : %s\n", result, gluErrorString(result));
}
