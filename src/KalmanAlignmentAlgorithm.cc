
#include "Alignment/KalmanAlignmentAlgorithm/interface/KalmanAlignmentAlgorithm.h"

// includes for alignment
#include "Alignment/CommonAlignment/interface/AlignableNavigator.h"
#include "Alignment/CommonAlignment/interface/Utilities.h"

#include "Alignment/CommonAlignmentAlgorithm/interface/AlignmentIORoot.h"
#include "Alignment/CommonAlignmentAlgorithm/interface/AlignmentParameterSelector.h"

#include "Alignment/KalmanAlignmentAlgorithm/interface/KalmanAlignmentUpdatorPlugin.h"
#include "Alignment/KalmanAlignmentAlgorithm/interface/KalmanAlignmentMetricsUpdatorPlugin.h"
#include "Alignment/KalmanAlignmentAlgorithm/interface/KalmanAlignmentUserVariables.h"
#include "Alignment/KalmanAlignmentAlgorithm/interface/KalmanAlignmentDataCollector.h"

#include "Alignment/CommonAlignmentParametrization/interface/RigidBodyAlignmentParameters.h"

#include "Alignment/ReferenceTrajectories/interface/TrajectoryFactoryPlugin.h"

#include "Alignment/TrackerAlignment/interface/TrackerAlignableId.h"
#include "Alignment/TrackerAlignment/interface/AlignableTracker.h"

#include "DataFormats/TrackingRecHit/interface/AlignmentPositionError.h"

// miscellaneous includes
#include "FWCore/Utilities/interface/Exception.h"
#include "Utilities/Timing/interface/TimingReport.h"
#include "CLHEP/Random/RandGauss.h"
#include <fstream>

using namespace std;


KalmanAlignmentAlgorithm::KalmanAlignmentAlgorithm( const edm::ParameterSet& config ) :
  AlignmentAlgorithmBase( config ),
  theConfiguration( config )
{}


KalmanAlignmentAlgorithm::~KalmanAlignmentAlgorithm( void ) {}


void KalmanAlignmentAlgorithm::initialize( const edm::EventSetup& setup, 
					   AlignableTracker* tracker, 
					   AlignableMuon* muon,
					   AlignmentParameterStore* store )
{
  theParameterStore = store;
  theNavigator = new AlignableNavigator( tracker->components() );
  theSelector = new AlignmentParameterSelector( tracker );

  initializeAlignmentParameters( setup );

  edm::ParameterSet config = theConfiguration.getParameter< edm::ParameterSet >( "TrackRefitter" );
  theRefitter = new KalmanAlignmentTrackRefitter( config );
  theRefitter->initialize( setup, theNavigator );

  KalmanAlignmentDataCollector::configure( theConfiguration.getParameter< edm::ParameterSet >( "DataCollector" ) );
}


void KalmanAlignmentAlgorithm::terminate( void )
{
  cout << "[KalmanAlignmentAlgorithm::terminate] start ..." << endl;

  set< Alignable* > allAlignables;
  vector< Alignable* > alignablesToWrite;

  const KalmanAlignmentTrackRefitter::TrackingSetupCollection& setups = theRefitter->getTrackingSetups();
  KalmanAlignmentTrackRefitter::TrackingSetupCollection::const_iterator itSetup;
  for ( itSetup = setups.begin(); itSetup != setups.end(); ++itSetup )
  {
    const vector< Alignable* >& alignablesFromMetrics  = (*itSetup)->metricsUpdator()->alignables();
    cout << "[KalmanAlignmentAlgorithm::terminate] The metrics updator for setup \'" << (*itSetup)->id()
	 << "\' holds " << alignablesFromMetrics.size() << " alignables" << endl;
    allAlignables.insert( alignablesFromMetrics.begin(), alignablesFromMetrics.end() );
  }

  for ( set< Alignable* >::iterator it = allAlignables.begin(); it != allAlignables.end(); ++it )
  {
    AlignmentParameters* alignmentParameters = ( *it )->alignmentParameters();

    if ( alignmentParameters != 0 )
    {
      KalmanAlignmentUserVariables* userVariables =
	dynamic_cast< KalmanAlignmentUserVariables* >( alignmentParameters->userVariables() );

      if ( userVariables != 0 && userVariables->numberOfUpdates() > 0 )
      {
	userVariables->update( true );
	userVariables->histogramParameters( "KalmanAlignmentAlgorithm" );
	alignablesToWrite.push_back( *it );
      }
    }
  }

  if ( theConfiguration.getUntrackedParameter< bool >( "WriteAlignmentParameters", false ) )
  {
    AlignmentIORoot alignmentIO;
    int ierr = 0;
    string output = theConfiguration.getParameter< string >( "OutputFile" );

    cout << "Write data for " << alignablesToWrite.size() << " alignables ..." << endl;

    // Write output to "iteration 1", ...
    alignmentIO.writeAlignmentParameters( alignablesToWrite, output.c_str(), 1, false, ierr );
    // ... or, if "iteration 1" already exists, write it to "highest iteration + 1"
    if ( ierr == -1 ) alignmentIO.writeAlignmentParameters( alignablesToWrite, output.c_str(), -1, false, ierr );
  }

  KalmanAlignmentDataCollector::write();

  TimingReport* timing = TimingReport::current();
  timing->dump( cout );  

  string timingLogFile = theConfiguration.getUntrackedParameter< string >( "TimingLogFile", "timing.log" );

  ofstream* output = new ofstream( timingLogFile.c_str() );
  timing->dump( *output );
  output->close();
  delete output;

  delete theNavigator;

  cout << "[KalmanAlignmentAlgorithm::terminate] ... done." << endl;
}


void KalmanAlignmentAlgorithm::run( const edm::EventSetup & setup,
				    const ConstTrajTrackPairCollection & tracks )
{
  static int iEvent = 1;
  if ( iEvent % 500 == 0 )  cout << "[KalmanAlignmentAlgorithm::run] Event Nr. " << iEvent << endl;
  iEvent++;

  try
  {
    // Run the refitter algorithm
    TrackletCollection refittedTracklets = theRefitter->refitTracks( setup, tracks );

    map< KalmanAlignmentTrackingSetup*, TrackletCollection > setupToTrackletMap;

    TrackletCollection::iterator itTracklet;
    for ( itTracklet = refittedTracklets.begin(); itTracklet != refittedTracklets.end(); ++itTracklet )
      setupToTrackletMap[(*itTracklet)->trackingSetup()].push_back( *itTracklet );

    map< KalmanAlignmentTrackingSetup*, TrackletCollection >::iterator itMap;
    for ( itMap = setupToTrackletMap.begin(); itMap != setupToTrackletMap.end(); ++itMap )
    {
      ConstTrajTrackPairCollection tracklets;
      ExternalPredictionCollection external;

      TrackletCollection::iterator itTracklet;
      for ( itTracklet = itMap->second.begin(); itTracklet != itMap->second.end(); ++itTracklet )
      {
	tracklets.push_back( (*itTracklet)->trajTrackPair() );
	external.push_back( (*itTracklet)->externalPrediction() );
      }

      ReferenceTrajectoryCollection trajectories = itMap->first->trajectoryFactory()->trajectories( setup, tracklets, external );
      ReferenceTrajectoryCollection::iterator itTrajectories;

      // Run the alignment algorithm.
      for ( itTrajectories = trajectories.begin(); itTrajectories != trajectories.end(); ++itTrajectories )
      {
	itMap->first->alignmentUpdator()->process( *itTrajectories, theParameterStore, theNavigator, itMap->first->metricsUpdator() );

	KalmanAlignmentDataCollector::fillHistogram( "Trajectory_RecHits", (*itTrajectories)->recHits().size() );
      }
    }
  }
  catch( cms::Exception& exception )
  {
    cout << exception.what() << endl;
    terminate();
    throw exception;
  }

}


void KalmanAlignmentAlgorithm::initializeAlignmentParameters( const edm::EventSetup& setup )
{
  TrackerAlignableId* alignableId = new TrackerAlignableId;

  const edm::ParameterSet initConfig = theConfiguration.getParameter< edm::ParameterSet >( "Initialization" );

  int updateGraph = initConfig.getUntrackedParameter< int >( "UpdateGraphs", 100 );

  bool addPositionError = false;// = initConfig.getUntrackedParameter< bool >( "AddPositionError", true );

  int seed  = initConfig.getUntrackedParameter< int >( "RandomSeed", 1726354 );
  HepRandom::createInstance();
  HepRandom::setTheSeed( seed );

  bool applyXShifts =  initConfig.getUntrackedParameter< bool >( "ApplyXShifts", false );
  bool applyYShifts =  initConfig.getUntrackedParameter< bool >( "ApplyYShifts", false );
  bool applyZShifts =  initConfig.getUntrackedParameter< bool >( "ApplyZShifts", false );
  bool applyXRots =  initConfig.getUntrackedParameter< bool >( "ApplyXRotations", false );
  bool applyYRots =  initConfig.getUntrackedParameter< bool >( "ApplyYRotations", false );
  bool applyZRots =  initConfig.getUntrackedParameter< bool >( "ApplyZRotations", false );

  bool applyRandomStartValues = initConfig.getUntrackedParameter< bool >( "ApplyRandomStartValues", false );
  if ( applyRandomStartValues )
    cout << "[KalmanAlignmentAlgorithm::initializeAlignmentParameters] ADDING RANDOM START VALUES!!!" << endl;

  bool applyCurl =  initConfig.getUntrackedParameter< bool >( "ApplyCurl", false );
  double curlConst =  initConfig.getUntrackedParameter< double >( "CurlConstant", 1e-6 );

  bool applyShifts = applyXShifts || applyYShifts || applyZShifts;
  bool applyRots = applyXRots || applyYRots || applyZRots;
  //bool applyMisalignment = applyShifts || applyRots || applyCurl;

  AlgebraicVector displacement( 3 );
  AlgebraicVector eulerAngles( 3 );

  AlgebraicVector startParameters( 6, 0 );
  AlgebraicSymMatrix startError( 6, 0 );

  AlgebraicVector randSig( 6, 0 );

  vector< string > initSelection = initConfig.getParameter< vector<string> >( "InitializationSelector" );

  vector< string >::iterator itInitSel;
  for ( itInitSel = initSelection.begin(); itInitSel != initSelection.end(); ++itInitSel )
  {
    const edm::ParameterSet config = initConfig.getParameter< edm::ParameterSet >( *itInitSel );

    addPositionError = initConfig.getUntrackedParameter< bool >( "AddPositionError", false );

    double sigmaXShift = config.getUntrackedParameter< double >( "SigmaXShifts", 4e-2 );
    double sigmaYShift = config.getUntrackedParameter< double >( "SigmaYShifts", 4e-2 );
    double sigmaZShift = config.getUntrackedParameter< double >( "SigmaZShifts", 4e-2 );
    double sigmaXRot = config.getUntrackedParameter< double >( "SigmaXRotations", 5e-4 );
    double sigmaYRot = config.getUntrackedParameter< double >( "SigmaYRotations", 5e-4 );
    double sigmaZRot = config.getUntrackedParameter< double >( "SigmaZRotations", 5e-4 );

    randSig[0] = sigmaXShift; randSig[1] = sigmaYShift; randSig[2] = sigmaZShift;
    randSig[3] = sigmaXRot; randSig[4] = sigmaYRot; randSig[5] = sigmaZRot;

    startError[0][0] = config.getUntrackedParameter< double >( "XShiftsStartError", 4e-4 );
    startError[1][1] = config.getUntrackedParameter< double >( "YShiftsStartError", 4e-4 );
    startError[2][2] = config.getUntrackedParameter< double >( "ZShiftsStartError", 4e-4 );
    startError[3][3] = config.getUntrackedParameter< double >( "XRotationsStartError", 3e-5 );
    startError[4][4] = config.getUntrackedParameter< double >( "YRotationsStartError", 3e-5 );
    startError[5][5] = config.getUntrackedParameter< double >( "ZRotationsStartError", 3e-5 );

    const vector< char > dummyParamSelector( 6, '0' );
    const vector< string > alignableSelector = config.getParameter< vector<string> >( "AlignableSelection" );

    vector< string >::const_iterator itAliSel;
    for ( itAliSel = alignableSelector.begin(); itAliSel != alignableSelector.end(); ++itAliSel )
    {
      theSelector->addSelection( *itAliSel, dummyParamSelector );
      cout << "[" << *itInitSel << "] add selection: " << *itAliSel << endl;
    }

    vector< Alignable* > alignables = theSelector->selectedAlignables();
    vector< Alignable* >::iterator itAlignable;

    sort( alignables.begin(), alignables.end(), *this );

    // Apply existing alignment parameters.
    map< Alignable*, vector< AlignmentParameters* > > alignmentParametersMap;

    int iApply = 0;
    bool readParam = config.getUntrackedParameter< bool >( "ReadParametersFromFile", false );
    bool readCovar = config.getUntrackedParameter< bool >( "ReadCovarianceFromFile", false );
    bool applyParam = config.getUntrackedParameter< bool >( "ApplyParametersFromFile", false );
    bool applyCovar = config.getUntrackedParameter< bool >( "ApplyErrorFromFile", false );
    if ( readParam || readCovar || applyParam || applyCovar )
    {
      string file = config.getUntrackedParameter< string >( "FileName", "Input.root" );
      int ierr = 0;
      int iter = 1;

      AlignmentIORoot alignmentIO;

      while ( !ierr )
      {
	cout << "[" << *itInitSel << "] read alignment parameters. file / iteration = " << file << " / " << iter << endl;
	vector< AlignmentParameters* > alignmentParameters = alignmentIO.readAlignmentParameters( alignables, file.c_str(), iter, ierr );
	cout << "[" << *itInitSel << "] #param / ierr = " << alignmentParameters.size() << " / " << ierr << endl;

	vector< AlignmentParameters* >::iterator itParam;
	for ( itParam = alignmentParameters.begin(); itParam != alignmentParameters.end(); ++itParam )
	  alignmentParametersMap[(*itParam)->alignable()].push_back( *itParam );

	++iter;
      }
    }

    int iAlign = 0;

    for ( itAlignable = alignables.begin(); itAlignable != alignables.end(); itAlignable++ )
    {
      displacement[0] = applyXShifts ? sigmaXShift*RandGauss::shoot() : 0.;
      displacement[1] = applyYShifts ? sigmaZShift*RandGauss::shoot() : 0.;
      displacement[2] = applyZShifts ? sigmaYShift*RandGauss::shoot() : 0.;

      if ( applyShifts ) 
      {
	align::LocalVector localShift( displacement[0], displacement[1], displacement[2] );
	align::GlobalVector globalShift = ( *itAlignable )->surface().toGlobal( localShift );
	( *itAlignable )->move( globalShift );
      }

      align::EulerAngles eulerAngles( 3 );

      eulerAngles[0] = applyXRots ? sigmaXRot*RandGauss::shoot() : 0.;
      eulerAngles[1] = applyYRots ? sigmaYRot*RandGauss::shoot() : 0.;
      eulerAngles[2] = applyZRots ? sigmaZRot*RandGauss::shoot() : 0.;

      if ( applyRots )
      {
	align::RotationType localRotation = align::toMatrix( eulerAngles );
	( *itAlignable )->rotateInLocalFrame( localRotation );
      }

      if ( applyCurl )
      {
	double radius = ( *itAlignable )->globalPosition().perp();
	( *itAlignable )->rotateAroundGlobalZ( curlConst*radius );
      }

      if ( addPositionError )
      {
	LocalVector localError( sqrt(startError[0][0]), sqrt(startError[1][1]), sqrt(startError[2][2]) );
	GlobalVector globalError = (*itAlignable)->surface().toGlobal( localError );
	AlignmentPositionError ape( globalError.x(), globalError.y(), globalError.z() );
	( *itAlignable )->setAlignmentPositionError( ape );
      }

      //AlgebraicVector trueParameters( 6 );
      //trueParameters[0] = -displacement[0];
      //trueParameters[1] = -displacement[1];
      //trueParameters[2] = -displacement[2];
      //trueParameters[3] = -eulerAngles[0];
      //trueParameters[4] = -eulerAngles[1];
      //trueParameters[5] = -eulerAngles[2];

      if ( (*itAlignable)->alignmentParameters() != 0 )
      {
	AlignmentParameters* alignmentParameters;
	if ( readParam && readCovar )
	{
	  if ( alignmentParametersMap.find( *itAlignable ) == alignmentParametersMap.end() )
	  {
	    //cout << "apply param and cov from FILE -> none stored, apply DEFAULT " << endl;
	    alignmentParameters = (*itAlignable)->alignmentParameters()->clone( startParameters, startError );
	    alignmentParameters->setUserVariables( new KalmanAlignmentUserVariables( *itAlignable, alignableId, updateGraph ) );
	  }
	  else
	  {
	    //cout << "apply param and cov from FILE" << endl;
	    alignmentParameters = alignmentParametersMap[*itAlignable].back();
	    KalmanAlignmentUserVariables* userVariables = new KalmanAlignmentUserVariables( *itAlignable, alignableId, updateGraph );
	    userVariables->update( alignmentParameters );
	    alignmentParameters->setUserVariables( userVariables );
	  }
	}
	else if ( readParam )
	{
	  if ( alignmentParametersMap.find( *itAlignable ) == alignmentParametersMap.end() )
	  {
	    alignmentParameters = (*itAlignable)->alignmentParameters()->clone( startParameters, startError );
	    alignmentParameters->setUserVariables( new KalmanAlignmentUserVariables( *itAlignable, alignableId, updateGraph ) );
	  }
	  else
	  {
	    AlgebraicVector parameters = alignmentParametersMap[*itAlignable].back()->parameters();
	    alignmentParameters = (*itAlignable)->alignmentParameters()->clone( parameters, startError );
	    KalmanAlignmentUserVariables* userVariables = new KalmanAlignmentUserVariables( *itAlignable, alignableId, updateGraph );
	    userVariables->update( alignmentParameters );
	    alignmentParameters->setUserVariables( userVariables );
	  }
	}
	else
	{
	  //cout << "apply DEFAULT param and cov" << endl;
	  alignmentParameters = (*itAlignable)->alignmentParameters()->clone( startParameters, startError );
	  //alignmentParameters = (*itAlignable)->alignmentParameters()->clone( trueParameters, startError );
	  alignmentParameters->setUserVariables( new KalmanAlignmentUserVariables( *itAlignable, alignableId, updateGraph ) );
	}

	(*itAlignable)->setAlignmentParameters( alignmentParameters );
	//if ( applyParam ) theParameterStore->applyParameters( *itAlignable );

	if ( applyRandomStartValues )
	{
	  cout << "applying random start values" << endl;

	  AlgebraicVector randomStartParameters = alignmentParameters->parameters();
	  AlgebraicSymMatrix randomStartErrors = alignmentParameters->covariance();

	  for ( int iParam = 0; iParam < randomStartParameters.num_row(); ++iParam )
	  {
	    randomStartParameters[iParam] += sqrt(randSig[iParam])*RandGauss::shoot();
	    //randomStartErrors[iParam][iParam] += randSig[iParam]*randSig[iParam];
	  }

	  cout << randomStartParameters << endl;

	  alignmentParameters = (*itAlignable)->alignmentParameters()->clone( randomStartParameters, randomStartErrors );
	  (*itAlignable)->setAlignmentParameters( alignmentParameters );
	}

      }

      if ( ( applyParam || applyCovar ) && alignmentParametersMap.find( *itAlignable ) != alignmentParametersMap.end() )
      {
	++iApply;

	vector< AlignmentParameters* > allAlignmentParameters = alignmentParametersMap[*itAlignable];
	vector< AlignmentParameters* >::iterator itParam;

	for ( itParam = allAlignmentParameters.begin(); itParam != allAlignmentParameters.end(); ++itParam )
	{
	  RigidBodyAlignmentParameters* alignmentParameters = dynamic_cast<RigidBodyAlignmentParameters*>( *itParam );

	  if ( !alignmentParameters )
	    throw cms::Exception( "BadConfig" ) << "applyParameters: provided alignable does not have rigid body alignment parameters";

	  if ( applyParam )
	  {
	    AlgebraicVector shift = alignmentParameters->translation();
	    const AlignableSurface& alignableSurface = ( *itAlignable )->surface();
	    ( *itAlignable )->move( alignableSurface.toGlobal( align::LocalVector( shift[0], shift[1], shift[2] ) ) );

	    align::EulerAngles angles = alignmentParameters->rotation();
	    if ( angles.normsq() > 1e-10 ) ( *itAlignable )->rotateInLocalFrame( align::toMatrix( angles ) );
	  }

	  if ( applyCovar )
	  {
	    const AlgebraicSymMatrix& aliCov = alignmentParameters->covariance();
	    LocalVector localError( sqrt(aliCov[0][0]), sqrt(aliCov[1][1]), sqrt(aliCov[2][2]) );
	    GlobalVector globalError = (*itAlignable)->surface().toGlobal( localError );
	    AlignmentPositionError ape( globalError.x(), globalError.y(), globalError.z() );
	    ( *itAlignable )->setAlignmentPositionError( ape );
	  }
	}

	KalmanAlignmentUserVariables* userVariables = dynamic_cast< KalmanAlignmentUserVariables* >( ( *itAlignable )->alignmentParameters()->userVariables() );
	if ( userVariables ) { ++iAlign; userVariables->setAlignmentFlag( true ); }
      }
    }

    cout << "[" << *itInitSel << "] Set the alignment flag for " << iAlign << " alignables." << endl;
    cout << "[" << *itInitSel << "] number of applied parameters: " << iApply << endl; 
    theSelector->clear();
  }

}
