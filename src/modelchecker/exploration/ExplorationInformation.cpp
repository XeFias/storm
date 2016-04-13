#include "src/modelchecker/exploration/ExplorationInformation.h"

#include "src/settings/SettingsManager.h"
#include "src/settings/modules/ExplorationSettings.h"

#include "src/utility/macros.h"

namespace storm {
    namespace modelchecker {
        namespace exploration_detail {
            
            template<typename StateType, typename ValueType>
            ExplorationInformation<StateType, ValueType>::ExplorationInformation(uint_fast64_t bitsPerBucket, storm::OptimizationDirection const& direction, ActionType const& unexploredMarker) : stateStorage(bitsPerBucket), unexploredMarker(unexploredMarker), optimizationDirection(direction), localPrecomputation(false), numberOfExplorationStepsUntilPrecomputation(100000), numberOfSampledPathsUntilPrecomputation(), nextStateHeuristic(storm::settings::modules::ExplorationSettings::NextStateHeuristic::DifferenceWeightedProbability) {
                
                storm::settings::modules::ExplorationSettings const& settings = storm::settings::explorationSettings();
                localPrecomputation = settings.isLocalPrecomputationSet();
                numberOfExplorationStepsUntilPrecomputation = settings.getNumberOfExplorationStepsUntilPrecomputation();
                if (settings.isNumberOfSampledPathsUntilPrecomputationSet()) {
                    numberOfSampledPathsUntilPrecomputation = settings.getNumberOfSampledPathsUntilPrecomputation();
                }
                
                nextStateHeuristic = settings.getNextStateHeuristic();
                STORM_LOG_ASSERT(useDifferenceWeightedProbabilityHeuristic() || useProbabilityHeuristic(), "Illegal next-state heuristic.");
            }
            
            template<typename StateType, typename ValueType>
            void ExplorationInformation<StateType, ValueType>::setInitialStates(std::vector<StateType> const& initialStates) {
                stateStorage.initialStateIndices = initialStates;
            }
            
            template<typename StateType, typename ValueType>
            StateType ExplorationInformation<StateType, ValueType>::getFirstInitialState() const {
                return stateStorage.initialStateIndices.front();
            }
            
            template<typename StateType, typename ValueType>
            std::size_t ExplorationInformation<StateType, ValueType>::getNumberOfInitialStates() const {
                return stateStorage.initialStateIndices.size();
            }
            
            template<typename StateType, typename ValueType>
            void ExplorationInformation<StateType, ValueType>::addUnexploredState(storm::generator::CompressedState const& compressedState) {
                stateToRowGroupMapping.push_back(unexploredMarker);
                unexploredStates[stateStorage.numberOfStates] = compressedState;
                ++stateStorage.numberOfStates;
            }
            
            template<typename StateType, typename ValueType>
            void ExplorationInformation<StateType, ValueType>::assignStateToRowGroup(StateType const& state, ActionType const& rowGroup) {
                stateToRowGroupMapping[state] = rowGroup;
            }
            
            template<typename StateType, typename ValueType>
            StateType ExplorationInformation<StateType, ValueType>::assignStateToNextRowGroup(StateType const& state) {
                stateToRowGroupMapping[state] = rowGroupIndices.size() - 1;
                return stateToRowGroupMapping[state];
            }
            
            template<typename StateType, typename ValueType>
            StateType ExplorationInformation<StateType, ValueType>::getNextRowGroup() const {
                return rowGroupIndices.size() - 1;
            }
            
            template<typename StateType, typename ValueType>
            void ExplorationInformation<StateType, ValueType>::newRowGroup(ActionType const& action) {
                rowGroupIndices.push_back(action);
            }
            
            template<typename StateType, typename ValueType>
            void ExplorationInformation<StateType, ValueType>::newRowGroup() {
                newRowGroup(matrix.size());
            }
            
            template<typename StateType, typename ValueType>
            std::size_t ExplorationInformation<StateType, ValueType>::getNumberOfUnexploredStates() const {
                return unexploredStates.size();
            }
            
            template<typename StateType, typename ValueType>
            std::size_t ExplorationInformation<StateType, ValueType>::getNumberOfDiscoveredStates() const {
                return stateStorage.numberOfStates;
            }
            
            template<typename StateType, typename ValueType>
            StateType const& ExplorationInformation<StateType, ValueType>::getRowGroup(StateType const& state) const {
                return stateToRowGroupMapping[state];
            }
            
            template<typename StateType, typename ValueType>
            StateType const& ExplorationInformation<StateType, ValueType>::getUnexploredMarker() const {
                return unexploredMarker;
            }
            
            template<typename StateType, typename ValueType>
            bool ExplorationInformation<StateType, ValueType>::isUnexplored(StateType const& state) const {
                return stateToRowGroupMapping[state] == unexploredMarker;
            }
            
            template<typename StateType, typename ValueType>
            bool ExplorationInformation<StateType, ValueType>::isTerminal(StateType const& state) const {
                return terminalStates.find(state) != terminalStates.end();
            }
            
            template<typename StateType, typename ValueType>
            typename ExplorationInformation<StateType, ValueType>::ActionType const& ExplorationInformation<StateType, ValueType>::getStartRowOfGroup(StateType const& group) const {
                return rowGroupIndices[group];
            }
            
            template<typename StateType, typename ValueType>
            std::size_t ExplorationInformation<StateType, ValueType>::getRowGroupSize(StateType const& group) const {
                return rowGroupIndices[group + 1] - rowGroupIndices[group];
            }
            
            template<typename StateType, typename ValueType>
            bool ExplorationInformation<StateType, ValueType>::onlyOneActionAvailable(StateType const& group) const {
                return getRowGroupSize(group) == 1;
            }
            
            template<typename StateType, typename ValueType>
            void ExplorationInformation<StateType, ValueType>::addTerminalState(StateType const& state) {
                terminalStates.insert(state);
            }
            
            template<typename StateType, typename ValueType>
            std::vector<storm::storage::MatrixEntry<StateType, ValueType>>& ExplorationInformation<StateType, ValueType>::getRowOfMatrix(ActionType const& row) {
                return matrix[row];
            }
            
            template<typename StateType, typename ValueType>
            std::vector<storm::storage::MatrixEntry<StateType, ValueType>> const& ExplorationInformation<StateType, ValueType>::getRowOfMatrix(ActionType const& row) const {
                return matrix[row];
            }
            
            template<typename StateType, typename ValueType>
            void ExplorationInformation<StateType, ValueType>::addRowsToMatrix(std::size_t const& count) {
                matrix.resize(matrix.size() + count);
            }
            
            template<typename StateType, typename ValueType>
            bool ExplorationInformation<StateType, ValueType>::maximize() const {
                return optimizationDirection == storm::OptimizationDirection::Maximize;
            }
            
            template<typename StateType, typename ValueType>
            bool ExplorationInformation<StateType, ValueType>::minimize() const {
                return !maximize();
            }
            
            template<typename StateType, typename ValueType>
            bool ExplorationInformation<StateType, ValueType>::performPrecomputationExcessiveExplorationSteps(std::size_t& numberExplorationStepsSinceLastPrecomputation) const {
                bool result = numberExplorationStepsSinceLastPrecomputation > numberOfExplorationStepsUntilPrecomputation;
                if (result) {
                    numberExplorationStepsSinceLastPrecomputation = 0;
                }
                return result;
            }
            
            template<typename StateType, typename ValueType>
            bool ExplorationInformation<StateType, ValueType>::performPrecomputationExcessiveSampledPaths(std::size_t& numberOfSampledPathsSinceLastPrecomputation) const {
                if (!numberOfSampledPathsUntilPrecomputation) {
                    return false;
                } else {
                    bool result = numberOfSampledPathsSinceLastPrecomputation > numberOfSampledPathsUntilPrecomputation.get();
                    if (result) {
                        numberOfSampledPathsSinceLastPrecomputation = 0;
                    }
                    return result;
                }
            }
            
            template<typename StateType, typename ValueType>
            bool ExplorationInformation<StateType, ValueType>::useLocalPrecomputation() const {
                return localPrecomputation;
            }
            
            template<typename StateType, typename ValueType>
            bool ExplorationInformation<StateType, ValueType>::useGlobalPrecomputation() const {
                return !useLocalPrecomputation();
            }
            
            template<typename StateType, typename ValueType>
            storm::settings::modules::ExplorationSettings::NextStateHeuristic const& ExplorationInformation<StateType, ValueType>::getNextStateHeuristic() const {
                return nextStateHeuristic;
            }
            
            template<typename StateType, typename ValueType>
            bool ExplorationInformation<StateType, ValueType>::useDifferenceWeightedProbabilityHeuristic() const {
                return nextStateHeuristic == storm::settings::modules::ExplorationSettings::NextStateHeuristic::DifferenceWeightedProbability;
            }
            
            template<typename StateType, typename ValueType>
            bool ExplorationInformation<StateType, ValueType>::useProbabilityHeuristic() const {
                return nextStateHeuristic == storm::settings::modules::ExplorationSettings::NextStateHeuristic::Probability;
            }
            
            template<typename StateType, typename ValueType>
            storm::OptimizationDirection const& ExplorationInformation<StateType, ValueType>::getOptimizationDirection() const {
                return optimizationDirection;
            }
            
            template<typename StateType, typename ValueType>
            void ExplorationInformation<StateType, ValueType>::setOptimizationDirection(storm::OptimizationDirection const& direction) {
                optimizationDirection = direction;
            }
            
            template class ExplorationInformation<uint32_t, double>;
        }
    }
}