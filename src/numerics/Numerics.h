#ifndef NUMERICS_NUMERICS_H
#define NUMERICS_NUMERICS_H

#include "NumConst.h"
#include "Matrix.h"
#include "LeastSquares.h"
#include "lp.h"
#include "cg.h"
#include "LDLT.h"

namespace Numerics {

typedef Vector<double>           RealVector;
typedef VectorWrapper<double>    RealVecWrapper;
typedef Matrix<double>           RealMatrix;
typedef SymmetricMatrix<double>  RealSymMat;

typedef NumConst<double>         RealConsts;

} // namespace Numerics

#endif // NUMERICS_NUMERICS_H