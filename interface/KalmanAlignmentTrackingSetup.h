#ifndef Alignment_KalmanAlignmentAlgorithm_KalmanAlignmentTrackingSetup_h
#define Alignment_KalmanAlignmentAlgorithm_KalmanAlignmentTrackingSetup_h

#include "TrackingTools/TransientTrackingRecHit/interface/TransientTrackingRecHit.h"
#include "TrackingTools/PatternTools/interface/TrajectoryFitter.h"
#include "TrackingTools/GeomPropagators/interface/Propagator.h"

#include "Alignment/ReferenceTrajectories/interface/TrajectoryFactoryBase.h"
#include "Alignment/KalmanAlignmentAlgorithm/interface/KalmanAlignmentUpdator.h"
#include "Alignment/KalmanAlignmentAlgorithm/interface/KalmanAlignmentMetricsUpdator.h"

#include <vector>
#include <string>


class KalmanAlignmentTrackingSetup
{

public:

  typedef int SubDetId;

  typedef TransientTrackingRecHit::ConstRecHitPointer ConstRecHitPointer;

  KalmanAlignmentTrackingSetup( const std::string& id,
				const TrajectoryFitter* fitter,
				const Propagator* propagator,
				const std::vector< SubDetId >& trackingIds,
				const unsigned int minTrackingHits,
				const bool sortInsideOut,
				const TrajectoryFitter* externalFitter,
				const Propagator* externalPropagator,
				const std::vector< SubDetId >& externalIds,
				const unsigned int minExternalHits,
				const bool externalSortInsideOut,
				TrajectoryFactoryBase* trajectoryFactory,
				KalmanAlignmentUpdator* alignmentUpdator,
				KalmanAlignmentMetricsUpdator* metricsUpdator );


  KalmanAlignmentTrackingSetup( const KalmanAlignmentTrackingSetup& setup );

  ~KalmanAlignmentTrackingSetup( void );

  inline const std::string id( void ) const { return theId; }

  inline const TrajectoryFitter* fitter( void ) const { return theFitter; }
  inline const TrajectoryFitter* externalFitter( void ) const { return theExternalFitter; }

  inline const Propagator* propagator( void ) const { return thePropagator; }
  inline const Propagator* externalPropagator( void ) const { return theExternalPropagator; }

  inline const std::vector< SubDetId >& getTrackingSubDetIds( void ) const { return theTrackingSubDetIds; }
  inline const std::vector< SubDetId >& getExternalTrackingSubDetIds( void ) const { return theExternalTrackingSubDetIds; }

  inline const unsigned int minTrackingHits( void ) const { return theMinTrackingHits; }
  inline const unsigned int minExternalHits( void ) const { return theMinExternalHits; }

  inline const bool sortInsideOut( void ) const { return theSortInsideOutFlag; }
  inline const bool externalSortInsideOut( void ) const { return theExternalSortInsideOutFlag; }

  bool useForTracking( const ConstRecHitPointer& recHit ) const;
  bool useForExternalTracking( const ConstRecHitPointer& recHit ) const;

  TrajectoryFactoryBase* trajectoryFactory( void ) const { return theTrajectoryFactory; }
  KalmanAlignmentUpdator* alignmentUpdator( void ) const { return theAlignmentUpdator; }
  KalmanAlignmentMetricsUpdator* metricsUpdator( void ) const { return theMetricsUpdator; }

private:

  std::string theId;

  TrajectoryFitter* theFitter;
  Propagator* thePropagator;
  std::vector< SubDetId > theTrackingSubDetIds;
  unsigned int theMinTrackingHits;
  bool theSortInsideOutFlag;

  TrajectoryFitter* theExternalFitter;
  Propagator* theExternalPropagator;
  std::vector< SubDetId > theExternalTrackingSubDetIds;
  unsigned int theMinExternalHits;
  bool theExternalSortInsideOutFlag;

  TrajectoryFactoryBase* theTrajectoryFactory;
  KalmanAlignmentUpdator* theAlignmentUpdator;
  KalmanAlignmentMetricsUpdator* theMetricsUpdator;

};

#endif
