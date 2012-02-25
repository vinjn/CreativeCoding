#ifdef USING_ARTK
#include <ARToolKitPlus/TrackerSingleMarker.h>
#endif

#include "BookARApp.h"
#include "cinder/ImageIo.h"
#include "cinder/params/Params.h"
#include "cinder/Utilities.h"
#include "cinder/Text.h"
#include "ARTracker.h"

void BookARApp::prepareSettings( Settings *settings )
{
	settings->setWindowSize( APP_W, APP_H );
	settings->setFullScreen( false );
	settings->setResizable( false );
	settings->setFrameRate( 60 );
	settings->setBorderless(false);
}

void BookARApp::setup()
{
	_tex_iphone4 = loadImage(getAppPath().generic_string()+"UI/iphone4.png");
	_area_capture.set(SPAC_LEFT,SPAC_UP,APP_W-SPAC_RIGHT,APP_H-SPAC_DOWN);

	mdl_files.resize(N_MODELS);
	mdl_files[0] = "/resources/transformer.mdl";
	mdl_files[1] = "/resources/tintin.mdl";
	mdl_files[2] = "/resources/MooseJaw.mdl";

	post_files[0] = "/poster/transformer.jpg";
	post_files[1] = "/poster/tintin.jpg";
	post_files[2] = "/resources/MooseJawMask.jpg";

	_ar_tracker = shared_ptr<ARTracker>(ARTracker::create("SDAR"));

	_obj_id = -1;

	_device_id = 0;

	vector<Capture::DeviceRef> devices( Capture::getDevices() ); 
	if (getArgs().size() == 2)
	{
		string arg = getArgs().back();
		_device_id = fromString<int>(arg);
		ci::constrain<int>(_device_id, 0, devices.size()-1);
	}

	if (_device_id < devices.size())
	{
		try {
			_capture = Capture( CAM_W, CAM_H, devices[_device_id] );
			_capture.start();
		}
		catch( ... ) {
			console() << "Failed to initialize capture" << std::endl;
		}
	}
	else
	{
		TextLayout layout;
		layout.setFont( Font( "Arial", 24 ) );
		layout.setColor( Color( 1, 1, 1 ) );
		layout.addLine("");
		layout.addLine("");
		layout.addLine( "No camera connection!");
		layout.addLine("");
		layout.addLine("");
		_tex_bg = layout.render(true);
	}

	_ar_tracker->setup(CAM_W, CAM_H, 10, 1000, static_cast<void*>(&mdl_files));

	_tex_default = loadImage(getAppPath().generic_string()+"resources/android.png");

	
	for (int i=0;i<N_MODELS;i++)
	{
		_img_posters[i] = loadImage(getAppPath().generic_string()+post_files[i]);
		if (i==N_MODELS-1)
			_tex_posters[i] = _img_posters[i];
		else
			_tex_posters[i] = _tex_default;
	}

#ifdef USING_ARTK
	_artk_tracker = shared_ptr<ARToolKitPlus::TrackerSingleMarker>(new ARToolKitPlus::TrackerSingleMarker(CAM_W, CAM_H, 8, 6, 6, 6, 0));
	{
		_artk_tracker->setPixelFormat(ARToolKitPlus::PIXEL_FORMAT_LUM);
		// load a camera file.
		std::string path(getAppPath()+"../../resources/no_distortion.cal");
		if (!_artk_tracker->init(path.c_str(), 1.0f, 1000.0f)) {
			console() << "ERROR: init() failed\n";
			return;
		}
		_artk_tracker->getCamera()->printSettings();
		_artk_tracker->setBorderWidth(0.125f);
		// set a threshold. we could also activate automatic thresholding
	#if 1
		_artk_tracker->setThreshold(150);
	#else
		_artk_tracker->activateAutoThreshold(true);
	#endif
		// let's use lookup-table undistortion for high-speed
		// note: LUT only works with images up to 1024x1024
		_artk_tracker->setUndistortionMode(ARToolKitPlus::UNDIST_LUT);
		// switch to simple ID based markers
		// use the tool in tools/IdPatGen to generate markers
		_artk_tracker->setMarkerMode(ARToolKitPlus::MARKER_ID_BCH);

		_artk_tracker->setPoseEstimator(ARToolKitPlus::POSE_ESTIMATOR_RPP);
	}
#endif

	//param	
	mParams = shared_ptr<params::InterfaceGl>(new params::InterfaceGl( "App parameters", Vec2i( 200, 100 ) ));
	{
		_cube_scale = 0.5;
		mParams->addParam( "Cube Scale", &_cube_scale, "min=0.1 max=2.0 step=0.1 keyIncr=s keyDecr=S");

		_cube_clr = ColorA( 0.25f, 0.5f, 1.0f, 1.0f );
		mParams->addParam( "Cube Color", &_cube_clr, "" );

		_mesh_translate = Vec3f( 0, 0, 50 );
		mParams->addParam( "Mesh Translate", &_mesh_translate, "");

		//mParams->addSeparator();

		_light_dir = Vec3f( .5f, -.5f, -.6f );
		mParams->addParam( "Light Direction", &_light_dir, "" );

		_capture_visible = true;
		//mParams->addParam( "capture stream visible", &_capture_visible, "");

		_2dbook_visible = true;
		mParams->addParam( "2d texture visible", &_2dbook_visible, "");

		_3dbook_visible = true;
		mParams->addParam( "3d mesh visible", &_3dbook_visible, ""); 

		_using_sdar = true;
		//mParams->addParam( "SNDA SDK", &_using_sdar, ""); 

	//	_proj_near = 10;
	//	mParams->addParam( "_proj_near", &_proj_near, "min=1.0 max=100 step=1");

	//	_proj_far = 1000;
	//	mParams->addParam( "_proj_far", &_proj_far, "min=10 max=10000 step=10");
	}
}


void BookARApp::shutdown()
{

}

CINDER_APP_CONSOLE( BookARApp, RendererGl )