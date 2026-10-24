#pragma once

#include "ModelAdapter.h"
#include <filesystem>

namespace EStudioUI
{
//! Bounds database-owned values before they reach a GUI snapshot.
struct SqliteAdapterLimits
{

    //! Maximum owned bytes in one cell; oversized values reject without truncation.
    std::size_t m_CellBytes = 1024 * 1024;

    //! Maximum owned value/key bytes in a result window.
    std::size_t m_PageBytes = 8 * 1024 * 1024;
};

//! Worker-safe SQLite table/view provider; every operation owns its connection and statements.
class SqliteAdapter final : public ModelAdapter
{
  public:
    //! Opens existing files only; writes require explicit opt-in and an addressable table.
    explicit SqliteAdapter(std::filesystem::path path, bool writable = false, SqliteAdapterLimits limits = {});

    //! Releases an immutable provider; no connection or worker is retained.

    ~SqliteAdapter() override;

    //! Returns the owned native path without a locale-dependent narrow conversion.
    const std::filesystem::path& GetPath() const;

    //! Reports explicit write opt-in independently of individual table/column constraints.
    bool IsWritable() const;

    //! Discovers bounded table/view schemas on the caller's worker.
    std::vector<ModelDataSet> GetDataSets(std::stop_token stop) const override;

    //! Executes typed bound filtering, ordered sorting and coherent count/page reads.
    AdapterPage Load(const ModelQuery& query, std::stop_token stop) const override;

    //! Applies optimistic exact-value writes and captures the resulting page in one transaction.
    AdapterPage Commit(const ModelQuery& query, std::span<const AdapterEdit> edits,
                       std::stop_token stop) const override;

  private:
    //! Owns the original filesystem path.
    std::filesystem::path m_Path;

    //! Owns the UTF-8 connection name independently of OS code pages.
    std::string m_Utf8Path;

    //! Enables existing-database writes explicitly.
    bool m_Writable;

    //! Bounds immutable result memory before publication.
    SqliteAdapterLimits m_Limits;
};
} // namespace EStudioUI
