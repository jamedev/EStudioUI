#pragma once

#include "ModelAdapter.h"

namespace EStudioUI
{
//! Exposes an immutable application snapshot through the same worker/paging interface as databases.
class SnapshotModelAdapter final : public ModelAdapter
{
  public:
    //! Owns a named immutable snapshot; all source IDs and values remain shared.
    SnapshotModelAdapter(std::string name, ModelSnapshot snapshot);

    //! Releases CPU-only source ownership on the last adapter owner's thread.

    ~SnapshotModelAdapter() override;

    //! Describes the single read-only source dataset.
    std::vector<ModelDataSet> GetDataSets(std::stop_token stop) const override;

    //! Filters/sorts explicitly on the worker and copies only the requested result window.
    AdapterPage Load(const ModelQuery& query, std::stop_token stop) const override;

  private:
    //! Owns trusted metadata independently of GUI objects.
    ModelDataSet m_DataSet;

    //! Shares immutable records with callers and worker readers.
    ModelSnapshot m_Snapshot;
};
} // namespace EStudioUI
