
/*
 * Copyright (C) 2000-2001 QuantLib Group
 *
 * This file is part of QuantLib.
 * QuantLib is a C++ open source library for financial quantitative
 * analysts and developers --- http://quantlib.sourceforge.net/
 *
 * QuantLib is free software and you are allowed to use, copy, modify, merge,
 * publish, distribute, and/or sell copies of it under the conditions stated
 * in the QuantLib License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the license for more details.
 *
 * You should have received a copy of the license along with this file;
 * if not, contact ferdinando@ametrano.net
 * The license is also available at http://quantlib.sourceforge.net/LICENSE.TXT
 *
 * The members of the QuantLib Group are listed in the Authors.txt file, also
 * available at http://quantlib.sourceforge.net/Authors.txt
*/

/*! \file bsmnumericaloption.cpp
    \brief common code for numerical option evaluation

    $Source$
    $Name$
    $Log$
    Revision 1.39  2001/04/23 14:20:34  marmar
    Cosmetic changes

    Revision 1.38  2001/04/09 14:13:34  nando
    all the *.hpp moved below the Include/ql level

    Revision 1.37  2001/04/06 18:46:22  nando
    changed Authors, Contributors, Licence and copyright header

    Revision 1.36  2001/04/04 12:13:24  nando
    Headers policy part 2:
    The Include directory is added to the compiler's include search path.
    Then both your code and user code specifies the sub-directory in
    #include directives, as in
    #include <Solvers1d/newton.hpp>

    Revision 1.35  2001/04/04 11:07:24  nando
    Headers policy part 1:
    Headers should have a .hpp (lowercase) filename extension
    All *.h renamed to *.hpp

    Revision 1.34  2001/03/21 11:31:55  marmar
    Main loop tranfered from method value to method calculate.
    Methods vega and rho belong now to class BSMOption

    Revision 1.33  2001/03/21 10:48:08  marmar
    valueAtCenter, firstDerivativeAtCenter, secondDerivativeAtCenter,
    are no longer methods of BSMNumericalOption but separate
    functions

    Revision 1.32  2001/03/12 13:11:08  marmar
    Public method getPrices added

    Revision 1.31  2001/03/07 17:14:57  marmar
    Grid limits are fine tuned

    Revision 1.30  2001/03/02 13:50:01  marmar
    Purely virtual method initializeStepCondition()
    introduced in the design of StepConditionOption

    Revision 1.29  2001/03/02 08:36:45  enri
    Shout options added:
    	* BSMAmericanOption is now AmericanOption, same interface
    	* ShoutOption added
    	* both ShoutOption and AmericanOption inherit from
    	  StepConditionOption
    offline.doxy.linux added.

    Revision 1.28  2001/03/01 14:20:36  marmar
    Private-member syntax changed

    Revision 1.27  2001/02/19 12:19:29  marmar
    Added trailing _ to protected and private members

    Revision 1.26  2001/02/15 15:57:41  marmar
    Defined QL_MIN_VOLATILITY 0.0005 and
    QL_MAX_VOLATILITY 3.0

    Revision 1.25  2001/02/15 15:30:30  marmar
    dVolMultiplier_ and dRMultiplier_ defined
    constant

    Revision 1.24  2001/02/14 10:11:25  marmar
    BSMNumericalOption has  a cleaner constructor

    Revision 1.23  2001/02/13 10:02:57  marmar
    Ambiguous variable name underlyingGrowthRate changed in
    unambiguos dividendYield

    Revision 1.22  2001/01/19 08:52:07  marmar
    Small bug fixed. Initialization of grid vectors is now after
     the determination of their size

    Revision 1.21  2001/01/08 11:44:18  lballabio
    Array back into QuantLib namespace - Math namespace broke expression templates, go figure

    Revision 1.20  2001/01/08 10:28:17  lballabio
    Moved Array to Math namespace

    Revision 1.19  2000/12/27 14:05:57  lballabio
    Turned Require and Ensure functions into QL_REQUIRE and QL_ENSURE macros

    Revision 1.18  2000/12/14 12:32:31  lballabio
    Added CVS tags in Doxygen file documentation blocks

*/

#include "ql/Pricers/bsmnumericaloption.hpp"
#include "ql/FiniteDifferences/valueatcenter.hpp"

namespace QuantLib {

    namespace Pricers {

        using FiniteDifferences::BoundaryCondition;
        using FiniteDifferences::BSMOperator;
        using FiniteDifferences::valueAtCenter;
        using FiniteDifferences::firstDerivativeAtCenter;
        using FiniteDifferences::secondDerivativeAtCenter;

        BSMNumericalOption::BSMNumericalOption(BSMNumericalOption::Type type,
            double underlying, double strike, Rate dividendYield,
            Rate riskFreeRate, Time residualTime, double volatility,
            int gridPoints)
        : BSMOption(type, underlying, strike, dividendYield, riskFreeRate,
            residualTime, volatility),
            gridPoints_(safeGridPoints(gridPoints, residualTime)),
            grid_(gridPoints_), initialPrices_(gridPoints_){
                hasBeenCalculated_ = false;
        }

        double BSMNumericalOption::value() const {
            if (!hasBeenCalculated_)
                calculate();
            return value_;
        }

        double BSMNumericalOption::delta() const {
            if (!hasBeenCalculated_)
                calculate();
            return delta_;
        }

        double BSMNumericalOption::gamma() const {
            if(!hasBeenCalculated_)
                calculate();
            return gamma_;
        }

        double BSMNumericalOption::theta() const {
            if(!hasBeenCalculated_)
                calculate();
            return theta_;
        }

        void BSMNumericalOption::setGridLimits() const {
            // correction for small volatilities
            double volSqrtTime = volatility_*QL_SQRT(residualTime_);
            // the prefactor fine tunes performance at small volatilities
            double prefactor = 1.0 + 0.02/volSqrtTime;
            double minMaxFactor = QL_EXP(4.0 * prefactor * volSqrtTime);

            sMin_ = underlying_/minMaxFactor;  // underlying grid min value
            sMax_ = underlying_*minMaxFactor;  // underlying grid max value
            // insure strike is included in the grid
            double safetyZoneFactor = 1.1;
            if(sMin_ > strike_/safetyZoneFactor){
                sMin_ = strike_/safetyZoneFactor;
                // enforce central placement of the underlying
                sMax_ = underlying_/(sMin_/underlying_);
            }
            if(sMax_ < strike_*safetyZoneFactor){
                sMax_ = strike_*safetyZoneFactor;
                // enforce central placement of the underlying
                sMin_ = underlying_/(sMax_/underlying_);
            }
        }

        void BSMNumericalOption::initializeGrid() const {
            gridLogSpacing_ = (QL_LOG(sMax_)-QL_LOG(sMin_))/(gridPoints_-1);
            double edx = QL_EXP(gridLogSpacing_);
            grid_[0] = sMin_;
            int j;
            for (j=1; j<gridPoints_; j++)
                grid_[j] = grid_[j-1]*edx;
        }

        void BSMNumericalOption::initializeInitialCondition() const {
            int j;
            switch (type_) {
              case Call:
                for(j = 0; j < gridPoints_; j++)
                    initialPrices_[j] = QL_MAX(grid_[j]-strike_,0.0);
                break;
              case Put:
                for(j = 0; j < gridPoints_; j++)
                    initialPrices_[j] = QL_MAX(strike_-grid_[j],0.0);
                break;
              case Straddle:
                for(j = 0; j < gridPoints_; j++)
                    initialPrices_[j] = QL_FABS(strike_-grid_[j]);
                break;
              default:
                QL_REQUIRE(1 == 0,
                           "BSMNumericalOption: invalid option type");
            }
        }

        void BSMNumericalOption::initializeOperator() const {
            finiteDifferenceOperator_ = BSMOperator(gridPoints_,
                gridLogSpacing_, riskFreeRate_, dividendYield_, volatility_);

            finiteDifferenceOperator_.setLowerBC(
                BoundaryCondition(BoundaryCondition::Neumann,
                    initialPrices_[1]-initialPrices_[0]));

            finiteDifferenceOperator_.setHigherBC(
                BoundaryCondition(BoundaryCondition::Neumann,
                    initialPrices_[gridPoints_-1] -
                        initialPrices_[gridPoints_-2]));
        }

    }

}


