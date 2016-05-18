#include "src/storage/prism/RestrictedParallelComposition.h"

#include <boost/algorithm/string/join.hpp>

namespace storm {
    namespace prism {
        
        RestrictedParallelComposition::RestrictedParallelComposition(std::shared_ptr<Composition> const& left, std::set<std::string> const& synchronizingActions, std::shared_ptr<Composition> const& right) : storm::prism::ParallelComposition(left, right), synchronizingActions(synchronizingActions) {
            // Intentionally left empty.
        }
        
        boost::any RestrictedParallelComposition::accept(CompositionVisitor& visitor) const {
            return visitor.visit(*this);
        }
        
        void RestrictedParallelComposition::writeToStream(std::ostream& stream) const {
            stream << "(" << *left << " |[" << boost::algorithm::join(synchronizingActions, ", ") << "]| " << *right << ")";
        }

    }
}