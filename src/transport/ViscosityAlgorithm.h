#ifndef TRANSPORT_VISCOSITY_ALGORITHM_H
#define TRANSPORT_VISCOSITY_ALGORITHM_H

#include "AutoRegistration.h"
#include "CollisionDB.h"

/**
 * Abstract base class for all viscosity algorithms which allows for self 
 * registration of concrete types.
 */
class ViscosityAlgorithm
{
public:
    
    typedef CollisionDB& ARGS;
    typedef Provider<ViscosityAlgorithm> PROVIDER;
    
    ViscosityAlgorithm(ARGS collisions)
        : m_collisions(collisions)
    { }
    
    virtual ~ViscosityAlgorithm() { }
    
    /**
     * Returns the mixture viscosity in Pa-s.
     */
    virtual double viscosity(
        const double T, const double nd, const double *const p_x) = 0;

protected:

    CollisionDB& m_collisions;
    
}; // class ViscosityAlgorithm

#endif // TRANSPORT_VISCOSITY_ALGORITHM_H