#include "src/models/sparse/DeterministicModel.h"
#include "src/utility/constants.h"
#include "src/adapters/CarlAdapter.h"

namespace storm {
    namespace models {
        namespace sparse {
            template <typename ValueType>
            DeterministicModel<ValueType>::DeterministicModel(storm::models::ModelType const& modelType,
                                                              storm::storage::SparseMatrix<ValueType> const& transitionMatrix,
                                                              storm::models::sparse::StateLabeling const& stateLabeling,
                                                              boost::optional<std::vector<ValueType>> const& optionalStateRewardVector,
                                                              boost::optional<storm::storage::SparseMatrix<ValueType>> const& optionalTransitionRewardMatrix,
                                                              boost::optional<std::vector<LabelSet>> const& optionalChoiceLabeling)
            : Model<ValueType>(modelType, transitionMatrix, stateLabeling, optionalStateRewardVector, optionalTransitionRewardMatrix, optionalChoiceLabeling) {
                // Intentionally left empty.
            }
            
            template <typename ValueType>
            DeterministicModel<ValueType>::DeterministicModel(storm::models::ModelType const& modelType,
                                                              storm::storage::SparseMatrix<ValueType>&& transitionMatrix,
                                                              storm::models::sparse::StateLabeling&& stateLabeling,
                                                              boost::optional<std::vector<ValueType>>&& optionalStateRewardVector,
                                                              boost::optional<storm::storage::SparseMatrix<ValueType>>&& optionalTransitionRewardMatrix,
                                                              boost::optional<std::vector<LabelSet>>&& optionalChoiceLabeling)
            : Model<ValueType>(modelType, std::move(transitionMatrix), std::move(stateLabeling), std::move(optionalStateRewardVector), std::move(optionalTransitionRewardMatrix), std::move(optionalChoiceLabeling)) {
                // Intentionally left empty.
            }
            
            template <typename ValueType>
            void DeterministicModel<ValueType>::writeDotToStream(std::ostream& outStream, bool includeLabeling, storm::storage::BitVector const* subsystem, std::vector<ValueType> const* firstValue, std::vector<ValueType> const* secondValue, std::vector<uint_fast64_t> const* stateColoring, std::vector<std::string> const* colors, std::vector<uint_fast64_t>* scheduler, bool finalizeOutput) const {
                Model<ValueType>::writeDotToStream(outStream, includeLabeling, subsystem, firstValue, secondValue, stateColoring, colors, scheduler, false);
                
                // Simply iterate over all transitions and draw the arrows with probability information attached.
                auto rowIt = this->getTransitionMatrix().begin();
                for (uint_fast64_t i = 0; i < this->getTransitionMatrix().getRowCount(); ++i, ++rowIt) {
                    typename storm::storage::SparseMatrix<ValueType>::const_rows row = this->getTransitionMatrix().getRow(i);
                    for (auto const& transition : row) {
                        if (transition.getValue() != storm::utility::zero<ValueType>()) {
                            if (subsystem == nullptr || subsystem->get(transition.getColumn())) {
                                outStream << "\t" << i << " -> " << transition.getColumn() << " [ label= \"" << transition.getValue() << "\" ];" << std::endl;
                            }
                        }
                    }
                }
                
                if (finalizeOutput) {
                    outStream << "}" << std::endl;
                }
            }
            
            template class DeterministicModel<double>;
            template class DeterministicModel<float>;
            
#ifdef STORM_HAVE_CARL
            template class DeterministicModel<storm::RationalFunction>;
#endif
            
        } // namespace sparse
    } // namespace models
} // namespace storm