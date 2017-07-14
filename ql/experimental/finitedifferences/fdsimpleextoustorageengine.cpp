/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
 Copyright (C) 2011 Klaus Spanderen
 Copyright (C) 2014 Ralph Schreyer

 This file is part of QuantLib, a free-software/open-source library
 for financial quantitative analysts and developers - http://quantlib.org/

 QuantLib is free software: you can redistribute it and/or modify it
 under the terms of the QuantLib license.  You should have received a
 copy of the license along with this program; if not, please email
 <quantlib-dev@lists.sf.net>. The license is also available online at
 <http://quantlib.org/license.shtml>.

 This program is distributed in the hope that it will be useful, but WITHOUT
 ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 FOR A PARTICULAR PURPOSE.  See the license for more details.
*/

/*! \file fdsimpleextoustorageengine.cpp
    \brief Finite Differences extended OU engine for simple storage options
*/

#include <ql/math/comparison.hpp>
#include <ql/termstructures/yieldtermstructure.hpp>
#include <ql/experimental/processes/extendedornsteinuhlenbeckprocess.hpp>
#include <ql/methods/finitedifferences/operators/fdmlinearoplayout.hpp>
#include <ql/methods/finitedifferences/meshers/uniform1dmesher.hpp>
#include <ql/methods/finitedifferences/utilities/fdminnervaluecalculator.hpp>
#include <ql/methods/finitedifferences/meshers/fdmmeshercomposite.hpp>
#include <ql/methods/finitedifferences/meshers/predefined1dmesher.hpp>
#include <ql/methods/finitedifferences/meshers/fdmsimpleprocess1dmesher.hpp>
#include <ql/pricingengines/vanilla/fdsimplebsswingengine.hpp>
#include <ql/methods/finitedifferences/stepconditions/fdmsimplestoragecondition.hpp>
#include <ql/methods/finitedifferences/stepconditions/fdmstepconditioncomposite.hpp>
#include <ql/experimental/finitedifferences/fdsimpleextoustorageengine.hpp>
#include <ql/methods/finitedifferences/solvers/fdmsolverdesc.hpp>
#include <ql/methods/finitedifferences/solvers/fdmbackwardsolver.hpp>
#include <ql/experimental/finitedifferences/fdmsimple2dextousolver.hpp>
#include <ql/experimental/finitedifferences/fdmexpextouinnervaluecalculator.hpp>

namespace QuantLib {

    namespace {
        class FdmStorageValue : public FdmInnerValueCalculator {
          public:
            explicit FdmStorageValue(const std::shared_ptr<FdmMesher>& mesher)
            : mesher_(mesher) { }

            Real innerValue(const FdmLinearOpIterator& iter, Time) {
                const Real s = std::exp(mesher_->location(iter, 0));
                const Real v = mesher_->location(iter, 1);
                return s*v;
            }
            Real avgInnerValue(const FdmLinearOpIterator& iter, Time t) {
                return innerValue(iter, t);
            }
          private:
            const std::shared_ptr<FdmMesher> mesher_;

        };

        class LessButNotCloseEnough {
          public:
            bool operator()(Real a, Real b) {
                return !(close_enough(a, b, 100) || b < a);
            }
        };
    }

    FdSimpleExtOUStorageEngine::FdSimpleExtOUStorageEngine(
            const std::shared_ptr<ExtendedOrnsteinUhlenbeckProcess>& process,
            const std::shared_ptr<YieldTermStructure>& rTS,
            Size tGrid, Size xGrid, Size yGrid,
            const std::shared_ptr<Shape>& shape,
            const FdmSchemeDesc& schemeDesc)
    : process_(process),
      rTS_  (rTS),
      tGrid_(tGrid),
      xGrid_(xGrid),
      yGrid_(yGrid),
      shape_(shape),
      schemeDesc_(schemeDesc) {
    }

    void FdSimpleExtOUStorageEngine::calculate() const {

        // 1. Exercise
        QL_REQUIRE(arguments_.exercise->type() == Exercise::Bermudan,
                   "Bermudan exercise supported only");

        // 2. Mesher
        const Time maturity
            = rTS_->dayCounter().yearFraction(rTS_->referenceDate(),
                                              arguments_.exercise->lastDate());

        const std::shared_ptr<Fdm1dMesher> xMesher =
                     std::make_shared<FdmSimpleProcess1dMesher>(xGrid_, process_, maturity);

        std::shared_ptr<Fdm1dMesher> storageMesher;

        if(yGrid_ == Null<Size>()){
            //elevator mesher
            std::vector<Real> storageValues(1, arguments_.capacity);
            storageValues.reserve(
                Size(arguments_.capacity/arguments_.changeRate)+1);

            for (Real level=0; level <= arguments_.capacity;
                    level+=arguments_.changeRate) {
                    storageValues.emplace_back(level);
                    storageValues.emplace_back(arguments_.capacity - level);
            }

            const std::set<Real, LessButNotCloseEnough>    orderedValues(
                storageValues.begin(), storageValues.end());
            storageValues.assign(orderedValues.begin(), orderedValues.end());

            storageMesher = std::make_shared<Predefined1dMesher>(storageValues);
        }
        else {
            // uniform mesher
            storageMesher = std::make_shared<Uniform1dMesher>(0, arguments_.capacity, yGrid_);
        }

        const std::shared_ptr<FdmMesher> mesher  =
            std::make_shared<FdmMesherComposite>(xMesher, storageMesher);

        // 3. Calculator
        std::shared_ptr<FdmInnerValueCalculator> storageCalculator =
                                                  std::make_shared<FdmStorageValue>(mesher);

        // 4. Step conditions
        std::list<std::shared_ptr<StepCondition<Array> > > stepConditions;
        std::list<std::vector<Time> > stoppingTimes;

        // 4.1 Bermudan step conditions
        std::vector<Time> exerciseTimes;
        for (Size i=0; i<arguments_.exercise->dates().size(); ++i) {
            const Time t = rTS_->dayCounter()
                           .yearFraction(rTS_->referenceDate(),
                                         arguments_.exercise->dates()[i]);

            QL_REQUIRE(t >= 0, "exercise dates must not contain past date");
            exerciseTimes.emplace_back(t);
        }
        stoppingTimes.emplace_back(exerciseTimes);

        std::shared_ptr<Payoff> payoff =
                                    std::make_shared<PlainVanillaPayoff>(Option::Call, 0.0);

        std::shared_ptr<FdmInnerValueCalculator> underlyingCalculator =
            std::make_shared<FdmExpExtOUInnerValueCalculator>(payoff, mesher, shape_);

        stepConditions.emplace_back(std::make_shared<FdmSimpleStorageCondition>(exerciseTimes,
                                          mesher, underlyingCalculator,
                                          arguments_.changeRate));

        std::shared_ptr<FdmStepConditionComposite> conditions =
                std::make_shared<FdmStepConditionComposite>(stoppingTimes, stepConditions);

        // 5. Boundary conditions
        const FdmBoundaryConditionSet boundaries;

        // 6. Solver
        FdmSolverDesc solverDesc = { mesher, boundaries, conditions,
                                     storageCalculator, maturity, tGrid_, 0 };

        std::shared_ptr<FdmSimple2dExtOUSolver> solver =
                std::make_shared<FdmSimple2dExtOUSolver>(
                           Handle<ExtendedOrnsteinUhlenbeckProcess>(process_),
                           rTS_, solverDesc, schemeDesc_);

        const Real x = process_->x0();
        const Real y = arguments_.load;

        results_.value = solver->valueAt(x, y);
    }
}
