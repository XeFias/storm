#include "storm/generator/JaniNextStateGenerator.h"

#include "storm/models/sparse/StateLabeling.h"

#include "storm/storage/expressions/SimpleValuation.h"
#include "storm/solver/SmtSolver.h"

#include "storm/storage/jani/Edge.h"
#include "storm/storage/jani/EdgeDestination.h"
#include "storm/storage/jani/Model.h"
#include "storm/storage/jani/Automaton.h"
#include "storm/storage/jani/Location.h"
#include "storm/storage/jani/AutomatonComposition.h"
#include "storm/storage/jani/ParallelComposition.h"
#include "storm/storage/jani/CompositionInformationVisitor.h"
#include "storm/storage/jani/traverser/AssignmentLevelFinder.h"
#include "storm/storage/jani/traverser/ArrayExpressionFinder.h"
#include "storm/storage/jani/traverser/RewardModelInformation.h"

#include "storm/storage/sparse/JaniChoiceOrigins.h"

#include "storm/builder/jit/Distribution.h"

#include "storm/utility/constants.h"
#include "storm/utility/macros.h"
#include "storm/utility/solver.h"
#include "storm/utility/combinatorics.h"
#include "storm/exceptions/InvalidSettingsException.h"
#include "storm/exceptions/WrongFormatException.h"
#include "storm/exceptions/InvalidArgumentException.h"
#include "storm/exceptions/UnexpectedException.h"
#include "storm/exceptions/NotSupportedException.h"

namespace storm {
    namespace generator {
        
        template<typename ValueType, typename StateType>
        JaniNextStateGenerator<ValueType, StateType>::JaniNextStateGenerator(storm::jani::Model const& model, NextStateGeneratorOptions const& options) : JaniNextStateGenerator(model.substituteConstantsFunctions(), options, false) {
            // Intentionally left empty.
        }
        
        template<typename ValueType, typename StateType>
        JaniNextStateGenerator<ValueType, StateType>::JaniNextStateGenerator(storm::jani::Model const& model, NextStateGeneratorOptions const& options, bool) : NextStateGenerator<ValueType, StateType>(model.getExpressionManager(), options), model(model), rewardExpressions(), hasStateActionRewards(false), evaluateRewardExpressionsAtEdges(false), evaluateRewardExpressionsAtDestinations(false) {
            STORM_LOG_THROW(!this->options.isBuildChoiceLabelsSet(), storm::exceptions::InvalidSettingsException, "JANI next-state generator cannot generate choice labels.");

            auto features = this->model.getModelFeatures();
            features.remove(storm::jani::ModelFeature::DerivedOperators);
            features.remove(storm::jani::ModelFeature::StateExitRewards);
            // Eliminate arrays if necessary.
            if (features.hasArrays()) {
                arrayEliminatorData = this->model.eliminateArrays(true);
                this->options.substituteExpressions([this](storm::expressions::Expression const& exp) {return arrayEliminatorData.transformExpression(exp);});
                features.remove(storm::jani::ModelFeature::Arrays);
            }
            STORM_LOG_THROW(features.empty(), storm::exceptions::InvalidSettingsException, "The explicit next-state generator does not support the following model feature(s): " << features.toString() << ".");

            // Get the reward expressions to be build. Also find out whether there is a non-trivial one.
            bool hasNonTrivialRewardExpressions = false;
            if (this->options.isBuildAllRewardModelsSet()) {
                rewardExpressions = this->model.getAllRewardModelExpressions();
                hasNonTrivialRewardExpressions = this->model.hasNonTrivialRewardExpression();
            } else {
                // Extract the reward models from the model based on the names we were given.
                for (auto const& rewardModelName : this->options.getRewardModelNames()) {
                    rewardExpressions.emplace_back(rewardModelName, this->model.getRewardModelExpression(rewardModelName));
                    hasNonTrivialRewardExpressions = hasNonTrivialRewardExpressions || this->model.isNonTrivialRewardModelExpression(rewardModelName);
                }
            }
            
            // We try to lift the edge destination assignments to the edges as this reduces the number of evaluator calls.
            // However, this will only be helpful if there are no assignment levels and only trivial reward expressions.
            if (hasNonTrivialRewardExpressions || this->model.usesAssignmentLevels()) {
                this->model.pushEdgeAssignmentsToDestinations();
            } else {
                this->model.liftTransientEdgeDestinationAssignments(storm::jani::AssignmentLevelFinder().getLowestAssignmentLevel(this->model));
                evaluateRewardExpressionsAtEdges = true;
            }
            
            // Create all synchronization-related information, e.g. the automata that are put in parallel.
            this->createSynchronizationInformation();

            // Now we are ready to initialize the variable information.
            this->checkValid();
            this->variableInformation = VariableInformation(this->model, this->parallelAutomata, options.getReservedBitsForUnboundedVariables(), options.isAddOutOfBoundsStateSet());
            this->variableInformation.registerArrayVariableReplacements(arrayEliminatorData);
            this->transientVariableInformation = TransientVariableInformation<ValueType>(this->model, this->parallelAutomata);
            this->transientVariableInformation.registerArrayVariableReplacements(arrayEliminatorData);
            
            // Create a proper evaluator.
            this->evaluator = std::make_unique<storm::expressions::ExpressionEvaluator<ValueType>>(this->model.getManager());
            this->transientVariableInformation.setDefaultValuesInEvaluator(*this->evaluator);
            
            
            // Build the information structs for the reward models.
            buildRewardModelInformation();
            
            // If there are terminal states we need to handle, we now need to translate all labels to expressions.
            if (this->options.hasTerminalStates()) {
                for (auto const& expressionOrLabelAndBool : this->options.getTerminalStates()) {
                    if (expressionOrLabelAndBool.first.isExpression()) {
                        this->terminalStates.push_back(std::make_pair(expressionOrLabelAndBool.first.getExpression(), expressionOrLabelAndBool.second));
                    } else {
                        // If it's a label, i.e. refers to a transient boolean variable we need to derive the expression
                        // for the label so we can cut off the exploration there.
                        if (expressionOrLabelAndBool.first.getLabel() != "init" && expressionOrLabelAndBool.first.getLabel() != "deadlock") {
                            STORM_LOG_THROW(this->model.getGlobalVariables().hasVariable(expressionOrLabelAndBool.first.getLabel()) , storm::exceptions::InvalidSettingsException, "Terminal states refer to illegal label '" << expressionOrLabelAndBool.first.getLabel() << "'.");
                            
                            storm::jani::Variable const& variable = this->model.getGlobalVariables().getVariable(expressionOrLabelAndBool.first.getLabel());
                            STORM_LOG_THROW(variable.isBooleanVariable(), storm::exceptions::InvalidSettingsException, "Terminal states refer to non-boolean variable '" << expressionOrLabelAndBool.first.getLabel() << "'.");
                            STORM_LOG_THROW(variable.isTransient(), storm::exceptions::InvalidSettingsException, "Terminal states refer to non-transient variable '" << expressionOrLabelAndBool.first.getLabel() << "'.");
                            
                            this->terminalStates.push_back(std::make_pair(this->model.getLabelExpression(variable.asBooleanVariable(), this->parallelAutomata), expressionOrLabelAndBool.second));
                        }
                    }
                }
            }
        }
        
        template<typename ValueType, typename StateType>
        ModelType JaniNextStateGenerator<ValueType, StateType>::getModelType() const {
            switch (model.getModelType()) {
                case storm::jani::ModelType::DTMC: return ModelType::DTMC;
                case storm::jani::ModelType::CTMC: return ModelType::CTMC;
                case storm::jani::ModelType::MDP: return ModelType::MDP;
                case storm::jani::ModelType::MA: return ModelType::MA;
                default:
                    STORM_LOG_THROW(false, storm::exceptions::WrongFormatException, "Invalid model type.");
            }
        }
        
        template<typename ValueType, typename StateType>
        bool JaniNextStateGenerator<ValueType, StateType>::isDeterministicModel() const {
            return model.isDeterministicModel();
        }
        
        template<typename ValueType, typename StateType>
        bool JaniNextStateGenerator<ValueType, StateType>::isDiscreteTimeModel() const {
            return model.isDiscreteTimeModel();
        }

        template<typename ValueType, typename StateType>
        bool JaniNextStateGenerator<ValueType, StateType>::isPartiallyObservable() const {
            return false;
        };
        
        template<typename ValueType, typename StateType>
        uint64_t JaniNextStateGenerator<ValueType, StateType>::getLocation(CompressedState const& state, LocationVariableInformation const& locationVariable) const {
            if (locationVariable.bitWidth == 0) {
                return 0;
            } else {
                return state.getAsInt(locationVariable.bitOffset, locationVariable.bitWidth);
            }
        }
        
        template<typename ValueType, typename StateType>
        void JaniNextStateGenerator<ValueType, StateType>::setLocation(CompressedState& state, LocationVariableInformation const& locationVariable, uint64_t locationIndex) const {
            if (locationVariable.bitWidth != 0) {
                state.setFromInt(locationVariable.bitOffset, locationVariable.bitWidth, locationIndex);
            }
        }
        
        template<typename ValueType, typename StateType>
        std::vector<uint64_t> JaniNextStateGenerator<ValueType, StateType>::getLocations(CompressedState const& state) const {
            std::vector<uint64_t> result(this->variableInformation.locationVariables.size());
            
            auto resultIt = result.begin();
            for (auto it = this->variableInformation.locationVariables.begin(), ite = this->variableInformation.locationVariables.end(); it != ite; ++it, ++resultIt) {
                if (it->bitWidth == 0) {
                    *resultIt = 0;
                } else {
                    *resultIt = state.getAsInt(it->bitOffset, it->bitWidth);
                }
            }
            
            return result;
        }
        
        template<typename ValueType, typename StateType>
        std::vector<StateType> JaniNextStateGenerator<ValueType, StateType>::getInitialStates(StateToIdCallback const& stateToIdCallback) {
            std::vector<StateType> initialStateIndices;

            if (this->model.hasNonTrivialInitialStates()) {
                // Prepare an SMT solver to enumerate all initial states.
                storm::utility::solver::SmtSolverFactory factory;
                std::unique_ptr<storm::solver::SmtSolver> solver = factory.create(model.getExpressionManager());
                
                std::vector<storm::expressions::Expression> rangeExpressions = model.getAllRangeExpressions(this->parallelAutomata);
                for (auto const& expression : rangeExpressions) {
                    solver->add(expression);
                }
                solver->add(model.getInitialStatesExpression(this->parallelAutomata));
                
                // Proceed as long as the solver can still enumerate initial states.
                while (solver->check() == storm::solver::SmtSolver::CheckResult::Sat) {
                    // Create fresh state.
                    CompressedState initialState(this->variableInformation.getTotalBitOffset(true));
                    
                    // Read variable assignment from the solution of the solver. Also, create an expression we can use to
                    // prevent the variable assignment from being enumerated again.
                    storm::expressions::Expression blockingExpression;
                    std::shared_ptr<storm::solver::SmtSolver::ModelReference> model = solver->getModel();
                    for (auto const& booleanVariable : this->variableInformation.booleanVariables) {
                        bool variableValue = model->getBooleanValue(booleanVariable.variable);
                        storm::expressions::Expression localBlockingExpression = variableValue ? !booleanVariable.variable : booleanVariable.variable;
                        blockingExpression = blockingExpression.isInitialized() ? blockingExpression || localBlockingExpression : localBlockingExpression;
                        initialState.set(booleanVariable.bitOffset, variableValue);
                    }
                    for (auto const& integerVariable : this->variableInformation.integerVariables) {
                        int_fast64_t variableValue = model->getIntegerValue(integerVariable.variable);
                        if (integerVariable.forceOutOfBoundsCheck || this->getOptions().isExplorationChecksSet()) {
                            STORM_LOG_THROW(variableValue >= integerVariable.lowerBound, storm::exceptions::WrongFormatException, "The initial value for variable " << integerVariable.variable.getName() << " is lower than the lower bound.");
                            STORM_LOG_THROW(variableValue <= integerVariable.upperBound, storm::exceptions::WrongFormatException, "The initial value for variable " << integerVariable.variable.getName() << " is higher than the upper bound");
                        }
                        storm::expressions::Expression localBlockingExpression = integerVariable.variable != model->getManager().integer(variableValue);
                        blockingExpression = blockingExpression.isInitialized() ? blockingExpression || localBlockingExpression : localBlockingExpression;
                        initialState.setFromInt(integerVariable.bitOffset, integerVariable.bitWidth, static_cast<uint_fast64_t>(variableValue - integerVariable.lowerBound));
                    }
                    
                    // Gather iterators to the initial locations of all the automata.
                    std::vector<std::set<uint64_t>::const_iterator> initialLocationsIts;
                    std::vector<std::set<uint64_t>::const_iterator> initialLocationsItes;
                    for (auto const& automatonRef : this->parallelAutomata) {
                        auto const& automaton = automatonRef.get();
                        initialLocationsIts.push_back(automaton.getInitialLocationIndices().cbegin());
                        initialLocationsItes.push_back(automaton.getInitialLocationIndices().cend());
                    }
                    storm::utility::combinatorics::forEach(initialLocationsIts, initialLocationsItes, [this,&initialState] (uint64_t index, uint64_t value) { setLocation(initialState, this->variableInformation.locationVariables[index], value); }, [&stateToIdCallback,&initialStateIndices,&initialState] () {
                        // Register initial state.
                        StateType id = stateToIdCallback(initialState);
                        initialStateIndices.push_back(id);
                        return true;
                    });
                    
                    // Block the current initial state to search for the next one.
                    if (!blockingExpression.isInitialized()) {
                        break;
                    }
                    solver->add(blockingExpression);
                }
                
                STORM_LOG_DEBUG("Enumerated " << initialStateIndices.size() << " initial states using SMT solving.");
            } else {
                CompressedState initialState(this->variableInformation.getTotalBitOffset(true));
                
                std::vector<int_fast64_t> currentIntegerValues;
                currentIntegerValues.reserve(this->variableInformation.integerVariables.size());
                for (auto const& variable : this->variableInformation.integerVariables) {
                    STORM_LOG_THROW(variable.lowerBound <= variable.upperBound, storm::exceptions::InvalidArgumentException, "Expecting variable with non-empty set of possible values.");
                    currentIntegerValues.emplace_back(0);
                    initialState.setFromInt(variable.bitOffset, variable.bitWidth, 0);
                }
                
                initialStateIndices.emplace_back(stateToIdCallback(initialState));
                
                bool done = false;
                while (!done) {
                    bool changedBooleanVariable = false;
                    for (auto const& booleanVariable : this->variableInformation.booleanVariables) {
                        if (initialState.get(booleanVariable.bitOffset)) {
                            initialState.set(booleanVariable.bitOffset);
                            changedBooleanVariable = true;
                            break;
                        } else {
                            initialState.set(booleanVariable.bitOffset, false);
                        }
                    }
                    
                    bool changedIntegerVariable = false;
                    if (changedBooleanVariable) {
                        initialStateIndices.emplace_back(stateToIdCallback(initialState));
                    } else {
                        for (uint64_t integerVariableIndex = 0; integerVariableIndex < this->variableInformation.integerVariables.size(); ++integerVariableIndex) {
                            auto const& integerVariable = this->variableInformation.integerVariables[integerVariableIndex];
                            if (currentIntegerValues[integerVariableIndex] < integerVariable.upperBound - integerVariable.lowerBound) {
                                ++currentIntegerValues[integerVariableIndex];
                                changedIntegerVariable = true;
                            } else {
                                currentIntegerValues[integerVariableIndex] = integerVariable.lowerBound;
                            }
                            initialState.setFromInt(integerVariable.bitOffset, integerVariable.bitWidth, currentIntegerValues[integerVariableIndex]);
                            
                            if (changedIntegerVariable) {
                                break;
                            }
                        }
                    }
                    
                    if (changedIntegerVariable) {
                        initialStateIndices.emplace_back(stateToIdCallback(initialState));
                    }
                    
                    done = !changedBooleanVariable && !changedIntegerVariable;
                }
                
                STORM_LOG_DEBUG("Enumerated " << initialStateIndices.size() << " initial states using brute force enumeration.");
            }
            
            return initialStateIndices;
        }
        
        template<typename ValueType, typename StateType>
        void JaniNextStateGenerator<ValueType, StateType>::applyUpdate(CompressedState& state, storm::jani::EdgeDestination const& destination, storm::generator::LocationVariableInformation const& locationVariable, int64_t assignmentLevel, storm::expressions::ExpressionEvaluator<ValueType> const& expressionEvaluator) {
            
            // Update the location of the state.
            setLocation(state, locationVariable, destination.getLocationIndex());
            
            // Then perform the assignments.
            auto const& assignments = destination.getOrderedAssignments().getNonTransientAssignments(assignmentLevel);
            auto assignmentIt = assignments.begin();
            auto assignmentIte = assignments.end();
            
            // Iterate over all boolean assignments and carry them out.
            auto boolIt = this->variableInformation.booleanVariables.begin();
            for (; assignmentIt != assignmentIte && assignmentIt->lValueIsVariable() && assignmentIt->getExpressionVariable().hasBooleanType(); ++assignmentIt) {
                while (assignmentIt->getExpressionVariable() != boolIt->variable) {
                    ++boolIt;
                }
                state.set(boolIt->bitOffset, expressionEvaluator.asBool(assignmentIt->getAssignedExpression()));
            }
            
            // Iterate over all integer assignments and carry them out.
            auto integerIt = this->variableInformation.integerVariables.begin();
            for (; assignmentIt != assignmentIte && assignmentIt->lValueIsVariable() && assignmentIt->getExpressionVariable().hasIntegerType(); ++assignmentIt) {
                while (assignmentIt->getExpressionVariable() != integerIt->variable) {
                    ++integerIt;
                }
                int_fast64_t assignedValue = expressionEvaluator.asInt(assignmentIt->getAssignedExpression());
                if (this->options.isAddOutOfBoundsStateSet()) {
                    if (assignedValue < integerIt->lowerBound || assignedValue > integerIt->upperBound) {
                        state = this->outOfBoundsState;
                    }
                } else if (integerIt->forceOutOfBoundsCheck || this->options.isExplorationChecksSet()) {
                    STORM_LOG_THROW(assignedValue >= integerIt->lowerBound, storm::exceptions::WrongFormatException, "The update " << assignmentIt->getExpressionVariable().getName() << " := " << assignmentIt->getAssignedExpression() << " leads to an out-of-bounds value (" << assignedValue << ") for the variable '" << assignmentIt->getExpressionVariable().getName() << "'.");
                    STORM_LOG_THROW(assignedValue <= integerIt->upperBound, storm::exceptions::WrongFormatException, "The update " << assignmentIt->getExpressionVariable().getName() << " := " << assignmentIt->getAssignedExpression() << " leads to an out-of-bounds value (" << assignedValue << ") for the variable '" << assignmentIt->getExpressionVariable().getName() << "'.");
                }
                state.setFromInt(integerIt->bitOffset, integerIt->bitWidth, assignedValue - integerIt->lowerBound);
                STORM_LOG_ASSERT(static_cast<int_fast64_t>(state.getAsInt(integerIt->bitOffset, integerIt->bitWidth)) + integerIt->lowerBound == assignedValue, "Writing to the bit vector bucket failed (read " << state.getAsInt(integerIt->bitOffset, integerIt->bitWidth) << " but wrote " << assignedValue << ").");
            }
            // Iterate over all array access assignments and carry them out.
            for (; assignmentIt != assignmentIte && assignmentIt->lValueIsArrayAccess(); ++assignmentIt) {
                int_fast64_t arrayIndex = expressionEvaluator.asInt(assignmentIt->getLValue().getArrayIndex());
                if (assignmentIt->getAssignedExpression().hasIntegerType()) {
                    IntegerVariableInformation const& intInfo = this->variableInformation.getIntegerArrayVariableReplacement(assignmentIt->getLValue().getArray().getExpressionVariable(), arrayIndex);
                    int_fast64_t assignedValue = expressionEvaluator.asInt(assignmentIt->getAssignedExpression());

                    if (this->options.isAddOutOfBoundsStateSet()) {
                        if (assignedValue < intInfo.lowerBound || assignedValue > intInfo.upperBound) {
                            state = this->outOfBoundsState;
                        }
                    } else if (this->options.isExplorationChecksSet()) {
                        STORM_LOG_THROW(assignedValue >= intInfo.lowerBound, storm::exceptions::WrongFormatException, "The update " << assignmentIt->getLValue() << " := " << assignmentIt->getAssignedExpression() << " leads to an out-of-bounds value (" << assignedValue << ") for the variable '" << assignmentIt->getExpressionVariable().getName() << "'.");
                        STORM_LOG_THROW(assignedValue <= intInfo.upperBound, storm::exceptions::WrongFormatException, "The update " << assignmentIt->getLValue() << " := " << assignmentIt->getAssignedExpression() << " leads to an out-of-bounds value (" << assignedValue << ") for the variable '" << assignmentIt->getExpressionVariable().getName() << "'.");
                    }
                    state.setFromInt(intInfo.bitOffset, intInfo.bitWidth, assignedValue - intInfo.lowerBound);
                    STORM_LOG_ASSERT(static_cast<int_fast64_t>(state.getAsInt(intInfo.bitOffset, intInfo.bitWidth)) + intInfo.lowerBound == assignedValue, "Writing to the bit vector bucket failed (read " << state.getAsInt(intInfo.bitOffset, intInfo.bitWidth) << " but wrote " << assignedValue << ").");
                } else if (assignmentIt->getAssignedExpression().hasBooleanType()) {
                    BooleanVariableInformation const& boolInfo = this->variableInformation.getBooleanArrayVariableReplacement(assignmentIt->getLValue().getArray().getExpressionVariable(), arrayIndex);
                    state.set(boolInfo.bitOffset, expressionEvaluator.asBool(assignmentIt->getAssignedExpression()));
                } else {
                    STORM_LOG_THROW(false, storm::exceptions::UnexpectedException, "Unhandled type of base variable.");
                }
            }
            
            // Check that we processed all assignments.
            STORM_LOG_ASSERT(assignmentIt == assignmentIte, "Not all assignments were consumed.");
        }
        
        template<typename ValueType, typename StateType>
        void JaniNextStateGenerator<ValueType, StateType>::applyTransientUpdate(TransientVariableValuation<ValueType>& transientValuation, storm::jani::detail::ConstAssignments const& transientAssignments,  storm::expressions::ExpressionEvaluator<ValueType> const& expressionEvaluator) {
            
            auto assignmentIt = transientAssignments.begin();
            auto assignmentIte = transientAssignments.end();
            
            // Iterate over all boolean assignments and carry them out.
            auto boolIt = this->transientVariableInformation.booleanVariableInformation.begin();
            for (; assignmentIt != assignmentIte && assignmentIt->lValueIsVariable() && assignmentIt->getExpressionVariable().hasBooleanType(); ++assignmentIt) {
                while (assignmentIt->getExpressionVariable() != boolIt->variable) {
                    ++boolIt;
                }
                transientValuation.booleanValues.emplace_back(&(*boolIt), expressionEvaluator.asBool(assignmentIt->getAssignedExpression()));
            }
            // Iterate over all integer assignments and carry them out.
            auto integerIt = this->transientVariableInformation.integerVariableInformation.begin();
            for (; assignmentIt != assignmentIte && assignmentIt->lValueIsVariable() && assignmentIt->getExpressionVariable().hasIntegerType(); ++assignmentIt) {
                while (assignmentIt->getExpressionVariable() != integerIt->variable) {
                    ++integerIt;
                }
                int64_t assignedValue = expressionEvaluator.asInt(assignmentIt->getAssignedExpression());
                if (this->options.isExplorationChecksSet()) {
                    STORM_LOG_THROW(assignedValue >= integerIt->lowerBound, storm::exceptions::WrongFormatException, "The update " << assignmentIt->getExpressionVariable().getName() << " := " << assignmentIt->getAssignedExpression() << " leads to an out-of-bounds value (" << assignedValue << ") for the variable '" << assignmentIt->getExpressionVariable().getName() << "'.");
                    STORM_LOG_THROW(assignedValue <= integerIt->upperBound, storm::exceptions::WrongFormatException, "The update " << assignmentIt->getExpressionVariable().getName() << " := " << assignmentIt->getAssignedExpression() << " leads to an out-of-bounds value (" << assignedValue << ") for the variable '" << assignmentIt->getExpressionVariable().getName() << "'.");
                }
                transientValuation.integerValues.emplace_back(&(*integerIt), assignedValue);
            }
            // Iterate over all rational assignments and carry them out.
            auto rationalIt = this->transientVariableInformation.rationalVariableInformation.begin();
            for (; assignmentIt != assignmentIte && assignmentIt->lValueIsVariable() && assignmentIt->getExpressionVariable().hasRationalType(); ++assignmentIt) {
                while (assignmentIt->getExpressionVariable() != rationalIt->variable) {
                    ++rationalIt;
                }
                transientValuation.rationalValues.emplace_back(&(*rationalIt), expressionEvaluator.asRational(assignmentIt->getAssignedExpression()));
            }
            
            // Iterate over all array access assignments and carry them out.
            for (; assignmentIt != assignmentIte && assignmentIt->lValueIsArrayAccess(); ++assignmentIt) {
                int_fast64_t arrayIndex = expressionEvaluator.asInt(assignmentIt->getLValue().getArrayIndex());
                storm::expressions::Type const& baseType = assignmentIt->getLValue().getArray().getExpressionVariable().getType();
                if (baseType.isIntegerType()) {
                    auto const& intInfo = this->transientVariableInformation.getIntegerArrayVariableReplacement(assignmentIt->getLValue().getArray().getExpressionVariable(), arrayIndex);
                    int64_t assignedValue = expressionEvaluator.asInt(assignmentIt->getAssignedExpression());
                    if (this->options.isExplorationChecksSet()) {
                        STORM_LOG_THROW(assignedValue >= intInfo.lowerBound, storm::exceptions::WrongFormatException, "The update " << assignmentIt->getLValue() << " := " << assignmentIt->getAssignedExpression() << " leads to an out-of-bounds value (" << assignedValue << ") for the variable '" << assignmentIt->getExpressionVariable().getName() << "'.");
                        STORM_LOG_THROW(assignedValue <= intInfo.upperBound, storm::exceptions::WrongFormatException, "The update " << assignmentIt->getLValue() << " := " << assignmentIt->getAssignedExpression() << " leads to an out-of-bounds value (" << assignedValue << ") for the variable '" << assignmentIt->getExpressionVariable().getName() << "'.");
                    }
                    transientValuation.integerValues.emplace_back(&intInfo, assignedValue);
                } else if (baseType.isBooleanType()) {
                    auto const& boolInfo = this->transientVariableInformation.getBooleanArrayVariableReplacement(assignmentIt->getLValue().getArray().getExpressionVariable(), arrayIndex);
                    transientValuation.booleanValues.emplace_back(&boolInfo, expressionEvaluator.asBool(assignmentIt->getAssignedExpression()));
                } else if (baseType.isRationalType()) {
                    auto const& rationalInfo = this->transientVariableInformation.getRationalArrayVariableReplacement(assignmentIt->getLValue().getArray().getExpressionVariable(), arrayIndex);
                    transientValuation.rationalValues.emplace_back(&rationalInfo, expressionEvaluator.asRational(assignmentIt->getAssignedExpression()));
                } else {
                    STORM_LOG_THROW(false, storm::exceptions::UnexpectedException, "Unhandled type of base variable.");
                }
            }
            
            // Check that we processed all assignments.
            STORM_LOG_ASSERT(assignmentIt == assignmentIte, "Not all assignments were consumed.");
        }
        
        template<typename ValueType, typename StateType>
        StateBehavior<ValueType, StateType> JaniNextStateGenerator<ValueType, StateType>::expand(StateToIdCallback const& stateToIdCallback) {
            // Prepare the result, in case we return early.
            StateBehavior<ValueType, StateType> result;
            
            // Retrieve the locations from the state.
            std::vector<uint64_t> locations = getLocations(*this->state);
            
            // First, construct the state rewards, as we may return early if there are no choices later and we already
            // need the state rewards then.
            uint64_t automatonIndex = 0;
            TransientVariableValuation<ValueType> transientVariableValuation;
            for (auto const& automatonRef : this->parallelAutomata) {
                auto const& automaton = automatonRef.get();
                uint64_t currentLocationIndex = locations[automatonIndex];
                storm::jani::Location const& location = automaton.getLocation(currentLocationIndex);
                STORM_LOG_ASSERT(!location.getAssignments().hasMultipleLevels(true), "Indexed assignments at locations are not supported in the jani standard.");
                applyTransientUpdate(transientVariableValuation, location.getAssignments().getTransientAssignments(), *this->evaluator);
                ++automatonIndex;
            }
            transientVariableValuation.setInEvaluator(*this->evaluator, this->getOptions().isExplorationChecksSet());
            result.addStateRewards(evaluateRewardExpressions());
            this->transientVariableInformation.setDefaultValuesInEvaluator(*this->evaluator);

            
            // If a terminal expression was set and we must not expand this state, return now.
            if (!this->terminalStates.empty()) {
                for (auto const& expressionBool : this->terminalStates) {
                    if (this->evaluator->asBool(expressionBool.first) == expressionBool.second) {
                        return result;
                    }
                }
            }
            
            // Get all choices for the state.
            result.setExpanded();
            std::vector<Choice<ValueType>> allChoices;
            if (this->getOptions().isApplyMaximalProgressAssumptionSet()) {
                // First explore only edges without a rate
                allChoices = getActionChoices(locations, *this->state, stateToIdCallback, EdgeFilter::WithoutRate);
                if (allChoices.empty()) {
                    // Expand the Markovian edges if there are no probabilistic ones.
                    allChoices = getActionChoices(locations, *this->state, stateToIdCallback, EdgeFilter::WithRate);
                }
            } else {
                allChoices = getActionChoices(locations, *this->state, stateToIdCallback);
            }
            std::size_t totalNumberOfChoices = allChoices.size();
            
            // If there is not a single choice, we return immediately, because the state has no behavior (other than
            // the state reward).
            if (totalNumberOfChoices == 0) {
                return result;
            }
            
            // If the model is a deterministic model, we need to fuse the choices into one.
            if (this->isDeterministicModel() && totalNumberOfChoices > 1) {
                Choice<ValueType> globalChoice;

                if (this->options.isAddOverlappingGuardLabelSet()) {
                    this->overlappingGuardStates->push_back(stateToIdCallback(*this->state));
                }
                
                // For CTMCs, we need to keep track of the total exit rate to scale the action rewards later. For DTMCs
                // this is equal to the number of choices, which is why we initialize it like this here.
                ValueType totalExitRate = this->isDiscreteTimeModel() ? static_cast<ValueType>(totalNumberOfChoices) : storm::utility::zero<ValueType>();
                
                // Iterate over all choices and combine the probabilities/rates into one choice.
                for (auto const& choice : allChoices) {
                    for (auto const& stateProbabilityPair : choice) {
                        if (this->isDiscreteTimeModel()) {
                            globalChoice.addProbability(stateProbabilityPair.first, stateProbabilityPair.second / totalNumberOfChoices);
                        } else {
                            globalChoice.addProbability(stateProbabilityPair.first, stateProbabilityPair.second);
                        }
                    }
                    
                    if (hasStateActionRewards && !this->isDiscreteTimeModel()) {
                        totalExitRate += choice.getTotalMass();
                    }
                }
                
                std::vector<ValueType> stateActionRewards(rewardExpressions.size(), storm::utility::zero<ValueType>());
                for (auto const& choice : allChoices) {
                    if (hasStateActionRewards) {
                        for (uint_fast64_t rewardVariableIndex = 0; rewardVariableIndex < rewardExpressions.size(); ++rewardVariableIndex) {
                            stateActionRewards[rewardVariableIndex] += choice.getRewards()[rewardVariableIndex] * choice.getTotalMass() / totalExitRate;
                        }
                    }
                    
                    if (this->options.isBuildChoiceOriginsSet() && choice.hasOriginData()) {
                        globalChoice.addOriginData(choice.getOriginData());
                    }
                }
                globalChoice.addRewards(std::move(stateActionRewards));
                
                // Move the newly fused choice in place.
                allChoices.clear();
                allChoices.push_back(std::move(globalChoice));
            }
            
            // Move all remaining choices in place.
            for (auto& choice : allChoices) {
                result.addChoice(std::move(choice));
            }
            
            this->postprocess(result);
            
            return result;
        }

        template<typename ValueType, typename StateType>
        Choice<ValueType> JaniNextStateGenerator<ValueType, StateType>::expandNonSynchronizingEdge(storm::jani::Edge const& edge, uint64_t outputActionIndex, uint64_t automatonIndex, CompressedState const& state, StateToIdCallback stateToIdCallback) {
            // Determine the exit rate if it's a Markovian edge.
            boost::optional<ValueType> exitRate = boost::none;
            if (edge.hasRate()) {
                exitRate = this->evaluator->asRational(edge.getRate());
            }
            
            Choice<ValueType> choice(edge.getActionIndex(), static_cast<bool>(exitRate));
            std::vector<ValueType> stateActionRewards;
            
            // Perform the transient edge assignments and create the state action rewards
            TransientVariableValuation<ValueType> transientVariableValuation;
            if (!evaluateRewardExpressionsAtEdges || edge.getAssignments().empty()) {
                stateActionRewards.resize(rewardModelInformation.size(), storm::utility::zero<ValueType>());
            } else {
                for (int64_t assignmentLevel = edge.getAssignments().getLowestLevel(true); assignmentLevel <= edge.getAssignments().getHighestLevel(true); ++assignmentLevel) {
                    transientVariableValuation.clear();
                    applyTransientUpdate(transientVariableValuation, edge.getAssignments().getTransientAssignments(assignmentLevel), *this->evaluator);
                    transientVariableValuation.setInEvaluator(*this->evaluator, this->getOptions().isExplorationChecksSet());
                }
                stateActionRewards = evaluateRewardExpressions();
                transientVariableInformation.setDefaultValuesInEvaluator(*this->evaluator);
            }
            
            // Iterate over all updates of the current command.
            ValueType probabilitySum = storm::utility::zero<ValueType>();
            for (auto const& destination : edge.getDestinations()) {
                ValueType probability = this->evaluator->asRational(destination.getProbability());
                
                if (probability != storm::utility::zero<ValueType>()) {
                    bool evaluatorChanged = false;
                    // Obtain target state index and add it to the list of known states. If it has not yet been
                    // seen, we also add it to the set of states that have yet to be explored.
                    int64_t assignmentLevel = edge.getLowestAssignmentLevel(); // Might be the largest possible integer, if there is no assignment
                    int64_t const& highestLevel = edge.getHighestAssignmentLevel();
                    bool hasTransientAssignments = destination.hasTransientAssignment();
                    CompressedState newState = state;
                    applyUpdate(newState, destination, this->variableInformation.locationVariables[automatonIndex], assignmentLevel, *this->evaluator);
                    if (hasTransientAssignments) {
                        STORM_LOG_ASSERT(this->options.isScaleAndLiftTransitionRewardsSet(), "Transition rewards are not supported and scaling to action rewards is disabled.");
                        transientVariableValuation.clear();
                        applyTransientUpdate(transientVariableValuation, destination.getOrderedAssignments().getTransientAssignments(assignmentLevel), *this->evaluator);
                        transientVariableValuation.setInEvaluator(*this->evaluator, this->getOptions().isExplorationChecksSet());
                        evaluatorChanged = true;
                    }
                    if (assignmentLevel < highestLevel) {
                        while (assignmentLevel < highestLevel) {
                            ++assignmentLevel;
                            unpackStateIntoEvaluator(newState, this->variableInformation, *this->evaluator);
                            evaluatorChanged = true;
                            applyUpdate(newState, destination, this->variableInformation.locationVariables[automatonIndex], assignmentLevel, *this->evaluator);
                            if (hasTransientAssignments) {
                                transientVariableValuation.clear();
                                applyTransientUpdate(transientVariableValuation, destination.getOrderedAssignments().getTransientAssignments(assignmentLevel), *this->evaluator);
                                transientVariableValuation.setInEvaluator(*this->evaluator, this->getOptions().isExplorationChecksSet());
                                evaluatorChanged = true;
                            }
                        }
                    }
                    if (evaluateRewardExpressionsAtDestinations) {
                        unpackStateIntoEvaluator(newState, this->variableInformation, *this->evaluator);
                        evaluatorChanged = true;
                        addEvaluatedRewardExpressions(stateActionRewards, probability);
                    }
                    
                    if (evaluatorChanged) {
                        // Restore the old variable valuation
                        unpackStateIntoEvaluator(state, this->variableInformation, *this->evaluator);
                        if (hasTransientAssignments) {
                            this->transientVariableInformation.setDefaultValuesInEvaluator(*this->evaluator);
                        }
                    }
                    
                    StateType stateIndex = stateToIdCallback(newState);
                    
                    // Update the choice by adding the probability/target state to it.
                    probability = exitRate ? exitRate.get() * probability : probability;
                    choice.addProbability(stateIndex, probability);
                    
                    if (this->options.isExplorationChecksSet()) {
                        probabilitySum += probability;
                    }
                }
            }
            
            // Add the state action rewards
            choice.addRewards(std::move(stateActionRewards));
            
            if (this->options.isExplorationChecksSet()) {
                // Check that the resulting distribution is in fact a distribution.
                STORM_LOG_THROW(!this->isDiscreteTimeModel() || this->comparator.isOne(probabilitySum), storm::exceptions::WrongFormatException, "Probabilities do not sum to one for edge (actually sum to " << probabilitySum << ").");
            }
            
            return choice;
        }
        
        template<typename ValueType, typename StateType>
        void JaniNextStateGenerator<ValueType, StateType>::generateSynchronizedDistribution(storm::storage::BitVector const& state, AutomataEdgeSets const& edgeCombination, std::vector<EdgeSetWithIndices::const_iterator> const& iteratorList, storm::builder::jit::Distribution<StateType, ValueType>& distribution, std::vector<ValueType>& stateActionRewards, EdgeIndexSet& edgeIndices, StateToIdCallback stateToIdCallback) {
            
            // Collect some information of the edges.
            int64_t lowestDestinationAssignmentLevel = std::numeric_limits<int64_t>::max();
            int64_t highestDestinationAssignmentLevel = std::numeric_limits<int64_t>::min();
            int64_t lowestEdgeAssignmentLevel = std::numeric_limits<int64_t>::max();
            int64_t highestEdgeAssignmentLevel = std::numeric_limits<int64_t>::min();
            uint64_t numDestinations = 1;
            for (uint_fast64_t i = 0; i < iteratorList.size(); ++i) {
                if (this->getOptions().isBuildChoiceOriginsSet()) {
                    edgeIndices.insert(model.encodeAutomatonAndEdgeIndices(edgeCombination[i].first, iteratorList[i]->first));
                }
                storm::jani::Edge const& edge = *iteratorList[i]->second;
                lowestDestinationAssignmentLevel = std::min(lowestDestinationAssignmentLevel, edge.getLowestAssignmentLevel());
                highestDestinationAssignmentLevel = std::max(highestDestinationAssignmentLevel, edge.getHighestAssignmentLevel());
                if (!edge.getAssignments().empty()) {
                    lowestEdgeAssignmentLevel = std::min(lowestEdgeAssignmentLevel, edge.getAssignments().getLowestLevel(true));
                    highestEdgeAssignmentLevel = std::max(highestEdgeAssignmentLevel, edge.getAssignments().getHighestLevel(true));
                }
                numDestinations *= edge.getNumberOfDestinations();
            }
            
            // Perform the edge assignments (if there are any)
            TransientVariableValuation<ValueType> transientVariableValuation;
            if (evaluateRewardExpressionsAtEdges && lowestEdgeAssignmentLevel <= highestEdgeAssignmentLevel) {
                for (int64_t assignmentLevel = lowestEdgeAssignmentLevel; assignmentLevel <= highestEdgeAssignmentLevel; ++assignmentLevel) {
                    transientVariableValuation.clear();
                    for (uint_fast64_t i = 0; i < iteratorList.size(); ++i) {
                        storm::jani::Edge const& edge = *iteratorList[i]->second;
                        applyTransientUpdate(transientVariableValuation, edge.getAssignments().getTransientAssignments(assignmentLevel), *this->evaluator);
                    }
                    transientVariableValuation.setInEvaluator(*this->evaluator, this->getOptions().isExplorationChecksSet());
                }
                addEvaluatedRewardExpressions(stateActionRewards, storm::utility::one<ValueType>());
                transientVariableInformation.setDefaultValuesInEvaluator(*this->evaluator);
            }
            
            std::vector<storm::jani::EdgeDestination const*> destinations;
            std::vector<LocationVariableInformation const*> locationVars;
            destinations.reserve(iteratorList.size());
            locationVars.reserve(iteratorList.size());

            for (uint64_t destinationId = 0; destinationId < numDestinations; ++destinationId) {
                // First assignment level
                destinations.clear();
                locationVars.clear();
                transientVariableValuation.clear();
                CompressedState successorState = state;
                ValueType successorProbability = storm::utility::one<ValueType>();

                uint64_t destinationIndex = destinationId;
                for (uint64_t i = 0; i < iteratorList.size(); ++i) {
                    storm::jani::Edge const& edge = *iteratorList[i]->second;
                    STORM_LOG_ASSERT(edge.getNumberOfDestinations() > 0, "Found an edge with zero destinations. This is not expected.");
                    uint64_t localDestinationIndex = destinationIndex % edge.getNumberOfDestinations();
                    destinations.push_back(&edge.getDestination(localDestinationIndex));
                    locationVars.push_back(&this->variableInformation.locationVariables[edgeCombination[i].first]);
                    destinationIndex /= edge.getNumberOfDestinations();
                    ValueType probability = this->evaluator->asRational(destinations.back()->getProbability());
                    if (edge.hasRate()) {
                        successorProbability *= probability * this->evaluator->asRational(edge.getRate());
                    } else {
                        successorProbability *= probability;
                    }
                    if (storm::utility::isZero(successorProbability)) {
                        break;
                    }
                    
                    applyUpdate(successorState, *destinations.back(), *locationVars.back(), lowestDestinationAssignmentLevel, *this->evaluator);
                    applyTransientUpdate(transientVariableValuation, destinations.back()->getOrderedAssignments().getTransientAssignments(lowestDestinationAssignmentLevel), *this->evaluator);
                }
                
                
                if (!storm::utility::isZero(successorProbability)) {
                    bool evaluatorChanged = false;
                    // remaining assignment levels (if there are any)
                    for (int64_t assignmentLevel = lowestDestinationAssignmentLevel + 1; assignmentLevel <= highestDestinationAssignmentLevel; ++assignmentLevel) {
                        unpackStateIntoEvaluator(successorState, this->variableInformation, *this->evaluator);
                        transientVariableValuation.setInEvaluator(*this->evaluator, this->getOptions().isExplorationChecksSet());
                        transientVariableValuation.clear();
                        evaluatorChanged = true;
                        auto locationVarIt = locationVars.begin();
                        for (auto const& destPtr : destinations) {
                            applyUpdate(successorState, *destPtr, **locationVarIt, assignmentLevel, *this->evaluator);
                            applyTransientUpdate(transientVariableValuation, destinations.back()->getOrderedAssignments().getTransientAssignments(assignmentLevel), *this->evaluator);
                            ++locationVarIt;
                        }
                    }
                    if (!transientVariableValuation.empty()) {
                        evaluatorChanged = true;
                        transientVariableValuation.setInEvaluator(*this->evaluator, this->getOptions().isExplorationChecksSet());
                    }
                    if (evaluateRewardExpressionsAtDestinations) {
                        unpackStateIntoEvaluator(successorState, this->variableInformation, *this->evaluator);
                        evaluatorChanged = true;
                        addEvaluatedRewardExpressions(stateActionRewards, successorProbability);
                    }
                    if (evaluatorChanged) {
                        // Restore the old state information
                        unpackStateIntoEvaluator(state, this->variableInformation, *this->evaluator);
                        this->transientVariableInformation.setDefaultValuesInEvaluator(*this->evaluator);
                    }
                    
                    StateType id = stateToIdCallback(successorState);
                    distribution.add(id, successorProbability);
                }
            }
        }
        
        template<typename ValueType, typename StateType>
        std::vector<Choice<ValueType>> JaniNextStateGenerator<ValueType, StateType>::expandSynchronizingEdgeCombination(AutomataEdgeSets const& edgeCombination, uint64_t outputActionIndex, CompressedState const& state, StateToIdCallback stateToIdCallback) {
            std::vector<Choice<ValueType>> result;
            
            if (this->options.isExplorationChecksSet()) {
                // Check whether a global variable is written multiple times in any combination.
                checkGlobalVariableWritesValid(edgeCombination);
            }
            
            std::vector<EdgeSetWithIndices::const_iterator> iteratorList(edgeCombination.size());
            
            // Initialize the list of iterators.
            for (size_t i = 0; i < edgeCombination.size(); ++i) {
                iteratorList[i] = edgeCombination[i].second.cbegin();
            }
            
            storm::builder::jit::Distribution<StateType, ValueType> distribution;

            // As long as there is one feasible combination of commands, keep on expanding it.
            bool done = false;
            while (!done) {
                distribution.clear();

                EdgeIndexSet edgeIndices;
                std::vector<ValueType> stateActionRewards(rewardExpressions.size(), storm::utility::zero<ValueType>());
                // old version without assignment levels generateSynchronizedDistribution(state, storm::utility::one<ValueType>(), 0, edgeCombination, iteratorList, distribution, stateActionRewards, edgeIndices, stateToIdCallback);
                generateSynchronizedDistribution(state, edgeCombination, iteratorList, distribution, stateActionRewards, edgeIndices, stateToIdCallback);
                distribution.compress();
                
                // At this point, we applied all commands of the current command combination and newTargetStates
                // contains all target states and their respective probabilities. That means we are now ready to
                // add the choice to the list of transitions.
                result.emplace_back(outputActionIndex);
                
                // Now create the actual distribution.
                Choice<ValueType>& choice = result.back();
                
                // Add the edge indices if requested.
                if (this->getOptions().isBuildChoiceOriginsSet()) {
                    choice.addOriginData(boost::any(std::move(edgeIndices)));
                }
                
                // Add the rewards to the choice.
                choice.addRewards(std::move(stateActionRewards));
                
                // Add the probabilities/rates to the newly created choice.
                ValueType probabilitySum = storm::utility::zero<ValueType>();
                for (auto const& stateProbability : distribution) {
                    choice.addProbability(stateProbability.getState(), stateProbability.getValue());
                    
                    if (this->options.isExplorationChecksSet()) {
                        probabilitySum += stateProbability.getValue();
                    }
                }
                
                if (this->options.isExplorationChecksSet()) {
                    // Check that the resulting distribution is in fact a distribution.
                    STORM_LOG_THROW(!this->isDiscreteTimeModel() || !this->comparator.isConstant(probabilitySum) || this->comparator.isOne(probabilitySum), storm::exceptions::WrongFormatException, "Sum of update probabilities do not sum to one for some edge (actually sum to " << probabilitySum << ").");
                }
                
                // Now, check whether there is one more command combination to consider.
                bool movedIterator = false;
                for (uint64_t j = 0; !movedIterator && j < iteratorList.size(); ++j) {
                    ++iteratorList[j];
                    if (iteratorList[j] != edgeCombination[j].second.end()) {
                        movedIterator = true;
                    } else {
                        // Reset the iterator to the beginning of the list.
                        iteratorList[j] = edgeCombination[j].second.begin();
                    }
                }
                
                done = !movedIterator;
            }
            
            return result;
        }
        
        template<typename ValueType, typename StateType>
        std::vector<Choice<ValueType>> JaniNextStateGenerator<ValueType, StateType>::getActionChoices(std::vector<uint64_t> const& locations, CompressedState const& state, StateToIdCallback stateToIdCallback, EdgeFilter const& edgeFilter) {
            std::vector<Choice<ValueType>> result;
            
            for (auto const& outputAndEdges : edges) {
                auto const& edges = outputAndEdges.second;
                if (edges.size() == 1) {
                    // If the synch consists of just one element, it's non-synchronizing.
                    auto const& nonsychingEdges = edges.front();
                    uint64_t automatonIndex = nonsychingEdges.first;

                    auto edgesIt = nonsychingEdges.second.find(locations[automatonIndex]);
                    if (edgesIt != nonsychingEdges.second.end()) {
                        for (auto const& indexAndEdge : edgesIt->second) {
                            if (edgeFilter != EdgeFilter::All) {
                                STORM_LOG_ASSERT(edgeFilter == EdgeFilter::WithRate || edgeFilter == EdgeFilter::WithoutRate, "Unexpected edge filter.");
                                if ((edgeFilter == EdgeFilter::WithRate) != indexAndEdge.second->hasRate()) {
                                    continue;
                                }
                            }
                            if (!this->evaluator->asBool(indexAndEdge.second->getGuard())) {
                                continue;
                            }
                        
                            Choice<ValueType> choice = expandNonSynchronizingEdge(*indexAndEdge.second, outputAndEdges.first ? outputAndEdges.first.get() : indexAndEdge.second->getActionIndex(), automatonIndex, state, stateToIdCallback);

                            if (this->getOptions().isBuildChoiceOriginsSet()) {
                                EdgeIndexSet edgeIndex { model.encodeAutomatonAndEdgeIndices(automatonIndex, indexAndEdge.first) };
                                choice.addOriginData(boost::any(std::move(edgeIndex)));
                            }
                            result.emplace_back(std::move(choice));
                        }
                    }
                } else {
                    // If the element has more than one set of edges, we need to perform a synchronization.
                    STORM_LOG_ASSERT(outputAndEdges.first, "Need output action index for synchronization.");
                    
                    AutomataEdgeSets automataEdgeSets;
                    uint64_t outputActionIndex = outputAndEdges.first.get();
                    
                    bool productiveCombination = true;
                    for (auto const& automatonAndEdges : outputAndEdges.second) {
                        uint64_t automatonIndex = automatonAndEdges.first;
                        EdgeSetWithIndices enabledEdgesOfAutomaton;
                        
                        bool atLeastOneEdge = false;
                        auto edgesIt = automatonAndEdges.second.find(locations[automatonIndex]);
                        if (edgesIt != automatonAndEdges.second.end()) {
                            for (auto const& indexAndEdge : edgesIt->second) {
                                if (edgeFilter != EdgeFilter::All) {
                                    STORM_LOG_ASSERT(edgeFilter == EdgeFilter::WithRate || edgeFilter == EdgeFilter::WithoutRate, "Unexpected edge filter.");
                                    if ((edgeFilter == EdgeFilter::WithRate) != indexAndEdge.second->hasRate()) {
                                        continue;
                                    }
                                }
                                if (!this->evaluator->asBool(indexAndEdge.second->getGuard())) {
                                    continue;
                                }
                            
                                atLeastOneEdge = true;
                                enabledEdgesOfAutomaton.emplace_back(indexAndEdge);
                            }
                        }

                        // If there is no enabled edge of this automaton, the whole combination is not productive.
                        if (!atLeastOneEdge) {
                            productiveCombination = false;
                            break;
                        }
                        
                        automataEdgeSets.emplace_back(std::make_pair(automatonIndex, std::move(enabledEdgesOfAutomaton)));
                    }
                    
                    if (productiveCombination) {
                        std::vector<Choice<ValueType>> choices = expandSynchronizingEdgeCombination(automataEdgeSets, outputActionIndex, state, stateToIdCallback);
                        
                        for (auto const& choice : choices) {
                            result.emplace_back(std::move(choice));
                        }
                    }
                 }
            }

            return result;
        }
        
        template<typename ValueType, typename StateType>
        void JaniNextStateGenerator<ValueType, StateType>::checkGlobalVariableWritesValid(AutomataEdgeSets const& enabledEdges) const {
            // Todo: this also throws if the writes are on different assignment level
            // Todo: this also throws if the writes are on different elements of the same array
            std::map<storm::expressions::Variable, uint64_t> writtenGlobalVariables;
            for (auto edgeSetIt = enabledEdges.begin(), edgeSetIte = enabledEdges.end(); edgeSetIt != edgeSetIte; ++edgeSetIt) {
                for (auto const& indexAndEdge : edgeSetIt->second) {
                    for (auto const& globalVariable : indexAndEdge.second->getWrittenGlobalVariables()) {
                        auto it = writtenGlobalVariables.find(globalVariable);
                        
                        auto index = std::distance(enabledEdges.begin(), edgeSetIt);
                        if (it != writtenGlobalVariables.end()) {
                            STORM_LOG_THROW(it->second == static_cast<uint64_t>(index), storm::exceptions::WrongFormatException, "Multiple writes to global variable '" << globalVariable.getName() << "' in synchronizing edges.");
                        } else {
                            writtenGlobalVariables.emplace(globalVariable, index);
                        }
                    }
                }
            }
        }
        
        template<typename ValueType, typename StateType>
        std::size_t JaniNextStateGenerator<ValueType, StateType>::getNumberOfRewardModels() const {
            return rewardExpressions.size();
        }
        
        template<typename ValueType, typename StateType>
        storm::builder::RewardModelInformation JaniNextStateGenerator<ValueType, StateType>::getRewardModelInformation(uint64_t const& index) const {
            return rewardModelInformation[index];
        }
        
        template<typename ValueType, typename StateType>
        storm::models::sparse::StateLabeling JaniNextStateGenerator<ValueType, StateType>::label(storm::storage::sparse::StateStorage<StateType> const& stateStorage, std::vector<StateType> const& initialStateIndices, std::vector<StateType> const& deadlockStateIndices) {
            // As in JANI we can use transient boolean variable assignments in locations to identify states, we need to
            // create a list of boolean transient variables and the expressions that define them.
            std::unordered_map<storm::expressions::Variable, storm::expressions::Expression> transientVariableToExpressionMap;
            bool translateArrays = !this->arrayEliminatorData.replacements.empty();
            for (auto const& variable : model.getGlobalVariables().getTransientVariables()) {
                if (variable.isBooleanVariable()) {
                    if (this->options.isBuildAllLabelsSet() || this->options.getLabelNames().find(variable.getName()) != this->options.getLabelNames().end()) {
                        storm::expressions::Expression labelExpression = model.getLabelExpression(variable.asBooleanVariable(), this->parallelAutomata);
                        if (translateArrays) {
                            labelExpression = this->arrayEliminatorData.transformExpression(labelExpression);
                        }
                        transientVariableToExpressionMap[variable.getExpressionVariable()] = std::move(labelExpression);
                    }
                }
            }
            
            std::vector<std::pair<std::string, storm::expressions::Expression>> transientVariableExpressions;
            for (auto const& element : transientVariableToExpressionMap) {
                transientVariableExpressions.push_back(std::make_pair(element.first.getName(), element.second));
            }
            return NextStateGenerator<ValueType, StateType>::label(stateStorage, initialStateIndices, deadlockStateIndices, transientVariableExpressions);
        }
        
        template<typename ValueType, typename StateType>
        std::vector<ValueType> JaniNextStateGenerator<ValueType, StateType>::evaluateRewardExpressions() const {
            std::vector<ValueType> result;
            result.reserve(rewardExpressions.size());
            for (auto const& rewardExpression : rewardExpressions) {
                result.push_back(this->evaluator->asRational(rewardExpression.second));
            }
            return result;
        }
        
        template<typename ValueType, typename StateType>
        void JaniNextStateGenerator<ValueType, StateType>::addEvaluatedRewardExpressions(std::vector<ValueType>& rewards, ValueType const& factor) const {
            assert(rewards.size() == rewardExpressions.size());
            auto rewIt = rewards.begin();
            for (auto const& rewardExpression : rewardExpressions) {
                (*rewIt) += factor * this->evaluator->asRational(rewardExpression.second);
                ++rewIt;
            }
        }
        
        template<typename ValueType, typename StateType>
        void JaniNextStateGenerator<ValueType, StateType>::buildRewardModelInformation() {
            for (auto const& rewardModel : rewardExpressions) {
                storm::jani::RewardModelInformation info(this->model, rewardModel.second);
                rewardModelInformation.emplace_back(rewardModel.first, info.hasStateRewards(), false, false);
                STORM_LOG_THROW(this->options.isScaleAndLiftTransitionRewardsSet() || !info.hasTransitionRewards(), storm::exceptions::NotSupportedException, "Transition rewards are not supported and a reduction to action-based rewards was not possible.");
                if (info.hasTransitionRewards()) {
                    evaluateRewardExpressionsAtDestinations = true;
                }
                if (info.hasActionRewards() || (this->options.isScaleAndLiftTransitionRewardsSet() && info.hasTransitionRewards())) {
                    hasStateActionRewards = true;
                    rewardModelInformation.back().setHasStateActionRewards();
                }
            }
            if (!hasStateActionRewards) {
                evaluateRewardExpressionsAtDestinations = false;
                evaluateRewardExpressionsAtEdges = false;
            }
        }
        
        template<typename ValueType, typename StateType>
        void JaniNextStateGenerator<ValueType, StateType>::createSynchronizationInformation() {
            // Create synchronizing edges information.
            storm::jani::Composition const& topLevelComposition = this->model.getSystemComposition();
            if (topLevelComposition.isAutomatonComposition()) {
                auto const& automaton = this->model.getAutomaton(topLevelComposition.asAutomatonComposition().getAutomatonName());
                this->parallelAutomata.push_back(automaton);
                
                LocationsAndEdges locationsAndEdges;
                uint64_t edgeIndex = 0;
                for (auto const& edge : automaton.getEdges()) {
                    locationsAndEdges[edge.getSourceLocationIndex()].emplace_back(std::make_pair(edgeIndex, &edge));
                    ++edgeIndex;
                }
                
                AutomataAndEdges automataAndEdges;
                automataAndEdges.emplace_back(std::make_pair(0, std::move(locationsAndEdges)));
                
                this->edges.emplace_back(std::make_pair(boost::none, std::move(automataAndEdges)));
            } else {
                STORM_LOG_THROW(topLevelComposition.isParallelComposition(), storm::exceptions::WrongFormatException, "Expected parallel composition.");
                storm::jani::ParallelComposition const& parallelComposition = topLevelComposition.asParallelComposition();
                
                uint64_t automatonIndex = 0;
                for (auto const& composition : parallelComposition.getSubcompositions()) {
                    STORM_LOG_THROW(composition->isAutomatonComposition(), storm::exceptions::WrongFormatException, "Expected flat parallel composition.");
                    this->parallelAutomata.push_back(this->model.getAutomaton(composition->asAutomatonComposition().getAutomatonName()));
                
                    // Add edges with silent action.
                    LocationsAndEdges locationsAndEdges;
                    uint64_t edgeIndex = 0;
                    for (auto const& edge : parallelAutomata.back().get().getEdges()) {
                        if (edge.getActionIndex() == storm::jani::Model::SILENT_ACTION_INDEX) {
                            locationsAndEdges[edge.getSourceLocationIndex()].emplace_back(std::make_pair(edgeIndex, &edge));
                        }
                        ++edgeIndex;
                    }

                    if (!locationsAndEdges.empty()) {
                        AutomataAndEdges automataAndEdges;
                        automataAndEdges.emplace_back(std::make_pair(automatonIndex, std::move(locationsAndEdges)));
                        this->edges.emplace_back(std::make_pair(boost::none, std::move(automataAndEdges)));
                    }
                    ++automatonIndex;
                }
                
                for (auto const& vector : parallelComposition.getSynchronizationVectors()) {
                    uint64_t outputActionIndex = this->model.getActionIndex(vector.getOutput());
                    
                    AutomataAndEdges automataAndEdges;
                    bool atLeastOneEdge = true;
                    uint64_t automatonIndex = 0;
                    for (auto const& element : vector.getInput()) {
                        if (!storm::jani::SynchronizationVector::isNoActionInput(element)) {
                            LocationsAndEdges locationsAndEdges;
                            uint64_t actionIndex = this->model.getActionIndex(element);
                            uint64_t edgeIndex = 0;
                            for (auto const& edge : parallelAutomata[automatonIndex].get().getEdges()) {
                                if (edge.getActionIndex() == actionIndex) {
                                    locationsAndEdges[edge.getSourceLocationIndex()].emplace_back(std::make_pair(edgeIndex, &edge));
                                }
                                ++edgeIndex;
                            }
                            if (locationsAndEdges.empty()) {
                                atLeastOneEdge = false;
                                break;
                            }
                            automataAndEdges.emplace_back(std::make_pair(automatonIndex, std::move(locationsAndEdges)));
                        }
                        ++automatonIndex;
                    }
                    
                    if (atLeastOneEdge) {
                        this->edges.emplace_back(std::make_pair(outputActionIndex, std::move(automataAndEdges)));
                    }
                }
            }
            
            STORM_LOG_TRACE("Number of synchronizations: " << this->edges.size() << ".");
        }
        
        template<typename ValueType, typename StateType>
        std::shared_ptr<storm::storage::sparse::ChoiceOrigins> JaniNextStateGenerator<ValueType, StateType>::generateChoiceOrigins(std::vector<boost::any>& dataForChoiceOrigins) const {
            if (!this->getOptions().isBuildChoiceOriginsSet()) {
                return nullptr;
            }
            
            std::vector<uint_fast64_t> identifiers;
            identifiers.reserve(dataForChoiceOrigins.size());
            
            std::map<EdgeIndexSet, uint_fast64_t> edgeIndexSetToIdentifierMap;
            // The empty edge set (i.e., the choices without origin) always has to get identifier getIdentifierForChoicesWithNoOrigin() -- which is assumed to be 0
            STORM_LOG_ASSERT(storm::storage::sparse::ChoiceOrigins::getIdentifierForChoicesWithNoOrigin() == 0, "The no origin identifier is assumed to be zero");
            edgeIndexSetToIdentifierMap.insert(std::make_pair(EdgeIndexSet(), 0));
            uint_fast64_t currentIdentifier = 1;
            for (boost::any& originData : dataForChoiceOrigins) {
                STORM_LOG_ASSERT(originData.empty() || boost::any_cast<EdgeIndexSet>(&originData) != nullptr, "Origin data has unexpected type: " << originData.type().name() << ".");
                
                EdgeIndexSet currentEdgeIndexSet = originData.empty() ? EdgeIndexSet() : boost::any_cast<EdgeIndexSet>(std::move(originData));
                auto insertionRes = edgeIndexSetToIdentifierMap.emplace(std::move(currentEdgeIndexSet), currentIdentifier);
                identifiers.push_back(insertionRes.first->second);
                if (insertionRes.second) {
                    ++currentIdentifier;
                }
            }
            
            std::vector<EdgeIndexSet> identifierToEdgeIndexSetMapping(currentIdentifier);
            for (auto const& setIdPair : edgeIndexSetToIdentifierMap) {
                identifierToEdgeIndexSetMapping[setIdPair.second] = setIdPair.first;
            }
            
            return std::make_shared<storm::storage::sparse::JaniChoiceOrigins>(std::make_shared<storm::jani::Model>(model), std::move(identifiers), std::move(identifierToEdgeIndexSetMapping));
        }
        
        template<typename ValueType, typename StateType>
        void JaniNextStateGenerator<ValueType, StateType>::checkValid() const {
            // If the program still contains undefined constants and we are not in a parametric setting, assemble an appropriate error message.
#ifdef STORM_HAVE_CARL
            if (!std::is_same<ValueType, storm::RationalFunction>::value && model.hasUndefinedConstants()) {
#else
                if (model.hasUndefinedConstants()) {
#endif
                    std::vector<std::reference_wrapper<storm::jani::Constant const>> undefinedConstants = model.getUndefinedConstants();
                    std::stringstream stream;
                    bool printComma = false;
                    for (auto const& constant : undefinedConstants) {
                        if (printComma) {
                            stream << ", ";
                        } else {
                            printComma = true;
                        }
                        stream << constant.get().getName() << " (" << constant.get().getType() << ")";
                    }
                    stream << ".";
                    STORM_LOG_THROW(false, storm::exceptions::InvalidArgumentException, "Program still contains these undefined constants: " + stream.str());
                }
                
#ifdef STORM_HAVE_CARL
                else if (std::is_same<ValueType, storm::RationalFunction>::value && !model.undefinedConstantsAreGraphPreserving()) {
                    STORM_LOG_THROW(false, storm::exceptions::InvalidArgumentException, "The input model contains undefined constants that influence the graph structure of the underlying model, which is not allowed.");
                }
#endif
            }
            
            template class JaniNextStateGenerator<double>;
            
#ifdef STORM_HAVE_CARL
            template class JaniNextStateGenerator<storm::RationalNumber>;
            template class JaniNextStateGenerator<storm::RationalFunction>;
#endif
        }
    }
