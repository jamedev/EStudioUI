#include <EStudioUI/ModelAdapter.h>
#include <stdexcept>

namespace EStudioUI
{
ModelAdapter::~ModelAdapter() = default;

AdapterPage ModelAdapter::Commit(const ModelQuery&, std::span<const AdapterEdit>, std::stop_token) const
{
    throw std::logic_error("This model adapter is read-only.");
}
} // namespace EStudioUI
