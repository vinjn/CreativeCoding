#include "cinder/app/AppBasic.h"
#include "cinder/ImageIo.h"
#include "cinder/MayaCamUI.h"
#include "cinder/Arcball.h"

#include "cinder/gl/gl.h"
#include "cinder/gl/Texture.h"
#include "cinder/gl/Fbo.h"
#include "cinder/gl/GlslProg.h"
#include "cinder/gl/Vbo.h"
#include "cinder/params/Params.h"

#include "cinder/Utilities.h"
#include "cinder/Thread.h"

#include "cinder/osc/OscListener.h"
#include "../../../_common/MiniConfig.h"
#include <fstream>
#include <boost/foreach.hpp>

#define ASIO_DISABLE_BOOST_REGEX 0
#include "../../../_common/asio/asio.hpp"

using namespace ci;
using namespace ci::app;
using namespace std;

const float kCamFov = 60.0f;
const int kOscPort = 3333;
const float kLedOffset = 225.0f;

fs::directory_iterator end_iter;

size_t gIdCount = 0;
struct Led
{
    Led(const Vec3f& aPos, float aValue = 1.0f) :
        pos(aPos), value(aValue), id(gIdCount++){}
    Vec3f pos;
    float value;
    size_t id;
};

struct Anim
{
    Anim()
    {
        reset();
    }
    string name;
    float index;
    vector<Channel> frames;

    void reset()
    {
        index = 0;
    }

    void update(float speed)
    {
        index += speed;
        if (index >= frames.size())
        {
            index = 0;
        }
    }

    const Channel& getFrame() const
    {
        return frames[static_cast<int>(index)];
    }

    const gl::Texture& getTexture()
    {
        int id = static_cast<int>(index);

        if (!tex)
        {
            tex = gl::Texture(frames[id]);
        }
        else
        {
            tex.update(frames[id], frames[id].getBounds());
        }

        return tex;
    }

private:
    gl::Texture tex;
};

struct CiApp : public AppBasic 
{
    void prepareSettings(Settings *settings)
    {
        readConfig();
        
        settings->setWindowPos(0, 0);
        settings->setWindowSize(800, 800);
    }

    void setup()
    {
        mParams = params::InterfaceGl("params", Vec2i(300, getConfigUIHeight()));

        mCurrentCamDistance = -1;

        // MiniConfig.xml
        setupConfigUI(&mParams);

        // parse "/assets/anim"
        fs::path root = getAssetPath("anim");
        for (fs::directory_iterator dir_iter(root); dir_iter != end_iter; ++dir_iter)
        {
            if (fs::is_directory(*dir_iter) || fs::is_symlink(*dir_iter))
            {
                Anim anim;
                if (!loadAnimFromDir(*dir_iter, anim))
                    continue;
                mAnims.push_back(anim);
            }
        }

        mCurrentAnim = 0;
        if (!mAnims.empty())
        {
            vector<string> animNames;
            for (int i=0; i<mAnims.size(); i++)
            {
                animNames.push_back(mAnims[i].name);
            }
            mParams.removeParam("ANIMATION");
            mParams.addParam("ANIMATION", animNames, &ANIMATION);
        }

        // parse "/assets/anim_wall"
        // TODO: merge
        root = getAssetPath("anim_wall");
        for (fs::directory_iterator dir_iter(root); dir_iter != end_iter; ++dir_iter)
        {
            if (fs::is_directory(*dir_iter))
            {
                Anim anim;
                if (!loadAnimFromDir(*dir_iter, anim))
                    continue;
                mAnimWalls.push_back(anim);
            }
        }

        mCurrentAnimWall = 0;
        if (!mAnimWalls.empty())
        {
            vector<string> animNames;
            for (int i=0; i<mAnimWalls.size(); i++)
            {
                animNames.push_back(mAnimWalls[i].name);
            }
            mParams.removeParam("ANIMATION_WALL");
            mParams.addParam("ANIMATION_WALL", animNames, &ANIMATION_WALL);
        }

        mParams.addSeparator();
        mParams.addButton("RESET_ROTATION", std::bind(&CiApp::resetArcball, this));

        {
            vector<string> axisNames;
            axisNames.push_back("x/red-axis");
            axisNames.push_back("y/green-axis");
            axisNames.push_back("z/blue-axis");

            mParams.removeParam("ROTATION_AXIS");
            mParams.addParam("ROTATION_AXIS", axisNames, &ROTATION_AXIS);
        }

        // osc setup
        mListener.setup(kOscPort);
        mListener.registerMessageReceived(this, &CiApp::onOscMessage);

        // parse leds.txt
        ifstream ifs(getAssetPath("leds.txt").string().c_str());
        int id;
        float x, y, z;

        Vec3f maxBound = Vec3f::zero();
        Vec3f minBound = Vec3f(FLT_MAX, FLT_MAX, FLT_MAX);

        int ledRadius = kLedOffset * REAL_TO_VIRTUAL / 2;
        while (ifs >> id >> x >> z >> y)
        {
            //x -= (245 - ledRadius);
            Vec3f pos(x, y, z);
            pos *= REAL_TO_VIRTUAL;
            mLeds.push_back(Led(pos));

            minBound.x = min<float>(minBound.x, pos.x - ledRadius);
            minBound.y = min<float>(minBound.y, pos.y);
            minBound.z = min<float>(minBound.z, pos.z - ledRadius);

            maxBound.x = max<float>(maxBound.x, pos.x + ledRadius);
            maxBound.y = max<float>(maxBound.y, pos.y);
            maxBound.z = max<float>(maxBound.z, pos.z + ledRadius);
        }

        mAABB = AxisAlignedBox3f(minBound, maxBound);
        BOOST_FOREACH(Led& led, mLeds)
        {
            //led.pos -= minBound;
        }

        mPrevSec = getElapsedSeconds();

        // wall
        {   
            gl::VboMesh::Layout layout;
            layout.setStaticTexCoords2d();
            layout.setStaticPositions();
            //layout.setStaticColorsRGB();

            const size_t kNumVertices = 4;
            vector<Vec3f> positions(kNumVertices);
            // CCW
            // #3: -271.0, 9748.0 ---- #2: 4129.0, 9748.0
            //
            // #1: -271.0, -1452.0 ---- #0: 4129.0, -1452.0 
            positions[0] = Vec3f(4129.0, -1452.0, 33626);
            positions[1] = Vec3f(-271.0, -1452.0, 33626);
            positions[2] = Vec3f(-271.0, 9748.0, 33626);
            positions[3] = Vec3f(4129.0, 9748.0, 33626);
            for (size_t i=0; i<kNumVertices; i++)
            {
                positions[i] *= REAL_TO_VIRTUAL;
            }

            vector<Vec2f> texCoords(kNumVertices);
            texCoords[0] = Vec2f(1, 1);
            texCoords[1] = Vec2f(0, 1);
            texCoords[2] = Vec2f(0, 0);
            texCoords[3] = Vec2f(1, 0);

            vector<Color> colors(kNumVertices);
            colors[0] = Color(1, 0, 0);
            colors[1] = Color(0, 1, 0);
            colors[2] = Color(0, 0, 1);
            colors[3] = Color(1, 1, 1);

            mVboWall = gl::VboMesh(kNumVertices, 0, layout, GL_QUADS);
            mVboWall.bufferPositions(positions);
            mVboWall.bufferTexCoords2d(0, texCoords);
            //mVboWall.bufferColorsRGB(colors);
        }
    }

    void resize(ResizeEvent event)
    {
        App::resize(event);
        mArcball.setWindowSize(getWindowSize());
        mArcball.setCenter(Vec2f(getWindowWidth() / 2.0f, getWindowHeight() / 2.0f));
        mArcball.setRadius(150);
    }

    void mouseDown(MouseEvent event)
    {
        if(event.isAltDown())
            mMayaCam.mouseDown(event.getPos());
        else
            mArcball.mouseDown(event.getPos());
    }

    void mouseDrag(MouseEvent event)
    {	
        if(event.isAltDown())
            mMayaCam.mouseDrag(event.getPos(), event.isLeftDown(), event.isMiddleDown(), event.isRightDown());
        else
            mArcball.mouseDrag(event.getPos());
    }

    void onOscMessage(const osc::Message* msg)
    {
        const string& addr = msg->getAddress();

        if (addr == "/pull")
        {
            int file_idx = msg->getArgAsInt32(0);
        }
    }

    void resetArcball()
    {
        mArcball.resetQuat();
    }

    void keyUp(KeyEvent event)
    {
        switch (event.getCode())
        {
        case KeyEvent::KEY_ESCAPE:
            {
                quit();
            }break;
        case KeyEvent::KEY_SPACE:
            {
                resetArcball();
            }break;
        case KeyEvent::KEY_h:
            {
                mParams.show(!mParams.isVisible());
            }break;
        case KeyEvent::KEY_x:
        case KeyEvent::KEY_r:
            {
                ROTATION_AXIS = 0;
            }break;
        case KeyEvent::KEY_y:
        case KeyEvent::KEY_g:
            {
                ROTATION_AXIS = 1;
            }break;
        case KeyEvent::KEY_z:
        case KeyEvent::KEY_b:
            {
                ROTATION_AXIS = 2;
            }break;
        }
    }

    void update()
    {
        mIoService.poll();

        const Vec3f axises[] = 
        {
            Vec3f::xAxis(),
            Vec3f::yAxis(),
            Vec3f::zAxis(),
        };
        mArcball.setConstraintAxis(axises[ROTATION_AXIS]);

        float delta = getElapsedSeconds() - mPrevSec;
        mPrevSec = getElapsedSeconds();
        if ((ANIMATION != mCurrentAnim) || (ANIMATION_WALL != mCurrentAnimWall))
        {
            mAnims[mCurrentAnim].reset();
            mCurrentAnim = ANIMATION;
            mAnimWalls[mCurrentAnimWall].reset();
            mCurrentAnimWall = ANIMATION_WALL;
        }
        mAnims[mCurrentAnim].update(delta * max<float>(ANIM_SPEED, 0));
        mAnimWalls[mCurrentAnimWall].update(delta * max<float>(ANIM_SPEED, 0));

        const Channel& suf = mAnims[mCurrentAnim].getFrame();
        Vec3f aabbSize = mAABB.getSize();
        Vec3f aabbMin = mAABB.getMin();

        int32_t width = suf.getWidth();
        int32_t height = suf.getHeight();

        float kW = suf.getWidth() / 1029.0f;
        float kH = suf.getHeight() / 124.0f;
        BOOST_FOREACH(Led& led, mLeds)
        {
            // online solver
            // http://www.bluebit.gr/matrix-calculator/linear_equations.aspx

            //3321  1  103
            //32936 1  1023
            float cx = 0.031065338510890f * led.pos.z / REAL_TO_VIRTUAL - 0.167989194664881f;

            //245  1  2
            //4070 1  122
            float cy = 0.031372549019608f * led.pos.x / REAL_TO_VIRTUAL - 5.686274509803920f;
            uint8_t value = *suf.getData(Vec2i(kW * cx, kH * cy));
            led.value = value / 255.f;
        }

        if (mCurrentCamDistance != CAM_DISTANCE)
        {
            mCurrentCamDistance = CAM_DISTANCE;
            CameraPersp initialCam;
            initialCam.setPerspective(kCamFov, getWindowAspectRatio(), 0.1f, 1000.0f);
            initialCam.lookAt(Vec3f(- mAABB.getMax().x * mCurrentCamDistance, mAABB.getMax().y * 0.5f, 0.0f), Vec3f::zero());
            mMayaCam.setCurrentCam(initialCam);
        }
    }

    void draw()
    {
        gl::enableDepthRead();
        gl::enableDepthWrite();

        gl::clear(ColorA::gray(43 / 255.f));
        gl::setMatrices(mMayaCam.getCamera());

        float kSceneOffsetY = 0;//SCENE_OFFSET_Y * REAL_TO_VIRTUAL;

        if (COORD_FRAME_VISIBLE)
        {
            gl::pushModelView();
            gl::translate(0, mAABB.getSize().y * -0.5f, kSceneOffsetY);
            gl::rotate(mArcball.getQuat());
            gl::scale(50, 50, 50);
            gl::drawCoordinateFrame();
            gl::popModelView();
        }

        gl::pushModelView();
        {
            Vec3f trans = mAABB.getSize() * -0.5f;
            trans.x *= -1;
            trans.y += kSceneOffsetY;
            gl::rotate(mArcball.getQuat());
            gl::translate(trans);

            gl::scale(-1, 1, 1);

            // lines
            gl::enableAlphaBlending();
            if (LINES_VISIBLE)
            {
                gl::disableDepthWrite();
                gl::color(ColorA::gray(76 / 255.f, 76 / 255.f));
                BOOST_FOREACH(const Led& led, mLeds)
                {
                    gl::drawLine(led.pos, Vec3f(led.pos.x, CEILING_HEIGHT, led.pos.z));
                }
            }

            // spheres
            gl::enableDepthWrite();
            BOOST_FOREACH(const Led& led, mLeds)
            {
                gl::color(ColorA::gray(1.0f, constrain<float>(led.value, SPHERE_MIN_ALPHA, 1.0f)));
                gl::drawSphere(led.pos, SPHERE_RADIUS);
            }
            gl::disableAlphaBlending();

            // wall
            gl::Texture tex = mAnimWalls[mCurrentAnimWall].getTexture();
            tex.enableAndBind();
            gl::draw(mVboWall);
            tex.disable();
        }
        gl::popModelView();

   
        // 2D
        gl::setMatricesWindow(getWindowSize());
        if (ANIM_COUNT_VISIBLE)
        {
            gl::drawString(toString(mAnims[mCurrentAnim].index), Vec2f(10, 10));
            gl::drawString(toString(mAnimWalls[mCurrentAnimWall].index), Vec2f(10, 30));
        }

        if (REFERENCE_VISIBLE)
        {
            const float kOffY = REFERENCE_OFFSET_Y;
            const Rectf kRefGlobeArea(28, 687 + kOffY, 28 + 636, 687 + 90 + kOffY);
            const Rectf kRefWallArea(689, 631 + kOffY, 689 + 84, 631 + 209 + kOffY);

            gl::draw(mAnims[mCurrentAnim].getTexture(), kRefGlobeArea);
            gl::draw(mAnimWalls[mCurrentAnimWall].getTexture(), kRefWallArea);
        }

        mParams.draw();
    }

    void safeLoadImage(fs::path imagePath, Anim& aAnim)
    {
        Channel suf = loadImage(imagePath);
        if (suf)
        {
            // TODO: orderd sequence
            aAnim.frames.push_back(suf);
        }
        console() << imagePath << endl;
    }

    bool loadAnimFromDir(fs::path dir, Anim& aAnim) 
    {
        aAnim.name = dir.filename().string();
        for (fs::directory_iterator it(dir); it != end_iter; ++it)
        {
            if (!fs::is_regular_file(*it))
                continue;

            mIoService.dispatch(bind(&CiApp::safeLoadImage, this, *it, aAnim));
        }

        mIoService.run();

        if (aAnim.frames.empty())
        {
            return false;
        }

        return true;
    }

private:
    params::InterfaceGl mParams;
    osc::Listener   mListener;
    vector<Led>     mLeds;
    MayaCamUI		mMayaCam;
    int             mCurrentCamDistance;
    Arcball         mArcball;
    AxisAlignedBox3f mAABB;

    vector<Anim>    mAnims;
    vector<Anim>    mAnimWalls;

    int             mCurrentAnim;

    // TODO: merge
    int             mCurrentAnimWall;

    float           mPrevSec;

    gl::VboMesh     mVboWall;

    asio::io_service mIoService;
};

CINDER_APP_BASIC(CiApp, RendererGl)
