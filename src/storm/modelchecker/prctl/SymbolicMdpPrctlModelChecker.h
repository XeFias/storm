#ifndef STORM_MODELCHECKER_SYMBOLICMDPPRCTLMODELCHECKER_H_
#define STORM_MODELCHECKER_SYMBOLICMDPPRCTLMODELCHECKER_H_

#include "storm/modelchecker/propositional/SymbolicPropositionalModelChecker.h"

#include "storm/models/symbolic/Mdp.h"

#include "storm/solver/SymbolicMinMaxLinearEquationSolver.h"

namespace storm {
    
    class Environment;
    
    namespace modelchecker {
        template<typename ModelType>
        class SymbolicMdpPrctlModelChecker : public SymbolicPropositionalModelChecker<ModelType> {
            public:
            typedef typename ModelType::ValueType ValueType;
            static const storm::dd::DdType DdType = ModelType::DdType;

            explicit SymbolicMdpPrctlModelChecker(ModelType const& model);
            explicit SymbolicMdpPrctlModelChecker(ModelType const& model, std::unique_ptr<storm::solver::SymbolicGeneralMinMaxLinearEquationSolverFactory<DdType, ValueType>>&& linearEquationSolverFactory);
            
            // The implemented methods of the AbstractModelChecker interface.
            virtual bool canHandle(CheckTask<storm::logic::Formula, ValueType> const& checkTask) const override;
            virtual std::unique_ptr<CheckResult> computeBoundedUntilProbabilities(Environment const& env, CheckTask<storm::logic::BoundedUntilFormula, ValueType> const& checkTask) override;
            virtual std::unique_ptr<CheckResult> computeNextProbabilities(Environment const& env, CheckTask<storm::logic::NextFormula, ValueType> const& checkTask) override;
            virtual std::unique_ptr<CheckResult> computeUntilProbabilities(Environment const& env, CheckTask<storm::logic::UntilFormula, ValueType> const& checkTask) override;
            virtual std::unique_ptr<CheckResult> computeGloballyProbabilities(Environment const& env, CheckTask<storm::logic::GloballyFormula, ValueType> const& checkTask) override;
            virtual std::unique_ptr<CheckResult> computeCumulativeRewards(Environment const& env, storm::logic::RewardMeasureType rewardMeasureType, CheckTask<storm::logic::CumulativeRewardFormula, ValueType> const& checkTask) override;
            virtual std::unique_ptr<CheckResult> computeInstantaneousRewards(Environment const& env, storm::logic::RewardMeasureType rewardMeasureType, CheckTask<storm::logic::InstantaneousRewardFormula, ValueType> const& checkTask) override;
            virtual std::unique_ptr<CheckResult> computeReachabilityRewards(Environment const& env, storm::logic::RewardMeasureType rewardMeasureType, CheckTask<storm::logic::EventuallyFormula, ValueType> const& checkTask) override;
            

            private:
            // An object that is used for retrieving linear equation solvers.
            std::unique_ptr<storm::solver::SymbolicGeneralMinMaxLinearEquationSolverFactory<DdType, ValueType>> linearEquationSolverFactory;
        };
        
    } // namespace modelchecker
} // namespace storm

#endif /* STORM_MODELCHECKER_SYMBOLICMDPPRCTLMODELCHECKER_H_ */
