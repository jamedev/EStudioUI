#include <EStudioUI/SqliteAdapter.h>
#include <sqlite3.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <thread>

namespace EStudioUI
{
namespace
{
void Stop(std::stop_token stop)
{
    if (stop.stop_requested())
    {
        throw std::runtime_error("The database request was cancelled.");
    }
}

std::string Quote(std::string_view name)
{
    if (name.empty() || name.size() > 65536 || name.find('\0') != std::string_view::npos)
    {
        throw std::invalid_argument("An SQL identifier must be bounded and contain no embedded zeroes.");
    }
    std::string result("\"");
    for (char c : name)
    {
        result.push_back(c);
        if (c == '"')
        {
            result.push_back(c);
        }
    }
    return result + '"';
}

std::string Upper(std::string text)
{
    for (auto& c : text)
    {
        if (c >= 'a' && c <= 'z')
        {
            c = static_cast<char>(c - ('a' - 'A'));
        }
    }
    return text;
}

class Connection
{
  public:
    sqlite3* m_Db = nullptr;
    std::stop_token m_Stop;

    Connection(const std::string& path, bool write, std::stop_token stop) : m_Stop(stop)
    {
        Stop(stop);
        const auto code = sqlite3_open_v2(
            path.c_str(), &m_Db, (write ? SQLITE_OPEN_READWRITE : SQLITE_OPEN_READONLY) | SQLITE_OPEN_NOMUTEX, nullptr);
        if (code != SQLITE_OK)
        {
            const std::string error = m_Db ? sqlite3_errmsg(m_Db) : "Cannot allocate SQLite connection.";
            if (m_Db)
            {
                sqlite3_close(m_Db);
            }
            m_Db = nullptr;
            throw std::runtime_error(error);
        }
        sqlite3_extended_result_codes(m_Db, 1);
        sqlite3_db_config(m_Db, SQLITE_DBCONFIG_DEFENSIVE, 1, nullptr);
        sqlite3_db_config(m_Db, SQLITE_DBCONFIG_TRUSTED_SCHEMA, 0, nullptr);
        sqlite3_progress_handler(
            m_Db, 1000, [](void* value) { return static_cast<Connection*>(value)->m_Stop.stop_requested() ? 1 : 0; },
            this);
        sqlite3_busy_handler(
            m_Db,
            [](void* value, int attempt)
            {
                auto* owner = static_cast<Connection*>(value);
                if (owner->m_Stop.stop_requested() || attempt >= 500)
                {
                    return 0;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                return 1;
            },
            this);
        try
        {
            Execute("PRAGMA foreign_keys=ON");
        }
        catch (...)
        {
            sqlite3_close_v2(m_Db);
            m_Db = nullptr;
            throw;
        }
    }

    ~Connection()
    {
        if (m_Db)
        {
            sqlite3_progress_handler(m_Db, 0, nullptr, nullptr);
            sqlite3_close_v2(m_Db);
        }
    }

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    void Check(int code) const
    {
        if (code != SQLITE_OK && code != SQLITE_DONE && code != SQLITE_ROW)
        {
            Stop(m_Stop);
            throw std::runtime_error(sqlite3_errmsg(m_Db));
        }
    }

    void Execute(const char* sql)
    {
        Stop(m_Stop);
        Check(sqlite3_exec(m_Db, sql, nullptr, nullptr, nullptr));
    }
};

class Statement
{
  public:
    Connection& m_Owner;
    sqlite3_stmt* m_Value = nullptr;

    Statement(Connection& owner, const std::string& sql) : m_Owner(owner)
    {
        owner.Check(sqlite3_prepare_v2(owner.m_Db, sql.c_str(), static_cast<int>(sql.size()), &m_Value, nullptr));
    }

    ~Statement()
    {
        sqlite3_finalize(m_Value);
    }

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    void Bind(int column, const ModelValue& value)
    {
        int code = SQLITE_MISUSE;
        if (std::holds_alternative<std::monostate>(value))
        {
            code = sqlite3_bind_null(m_Value, column);
        }
        else if (const auto* boolean = std::get_if<bool>(&value))
        {
            code = sqlite3_bind_int(m_Value, column, *boolean ? 1 : 0);
        }
        else if (const auto* integer = std::get_if<std::int64_t>(&value))
        {
            code = sqlite3_bind_int64(m_Value, column, *integer);
        }
        else if (const auto* real = std::get_if<double>(&value))
        {
            if (!std::isfinite(*real))
            {
                throw std::invalid_argument("SQLite real values must be finite.");
            }
            code = sqlite3_bind_double(m_Value, column, *real);
        }
        else if (const auto* text = std::get_if<std::string>(&value))
        {
            code = sqlite3_bind_text64(m_Value, column, text->data(), text->size(), SQLITE_TRANSIENT, SQLITE_UTF8);
        }
        else if (const auto* blob = std::get_if<ModelBlob>(&value))
        {
            code = blob->empty() ? sqlite3_bind_zeroblob(m_Value, column, 0)
                                 : sqlite3_bind_blob64(m_Value, column, blob->data(), blob->size(), SQLITE_TRANSIENT);
        }
        else
        {
            throw std::invalid_argument("SQLite fields do not store framework colors.");
        }
        m_Owner.Check(code);
    }

    bool Next()
    {
        Stop(m_Owner.m_Stop);
        const int code = sqlite3_step(m_Value);
        m_Owner.Check(code);
        return code == SQLITE_ROW;
    }

    std::string Text(int column) const
    {
        const auto* value = reinterpret_cast<const char*>(sqlite3_column_text(m_Value, column));
        return value ? std::string(value, static_cast<std::size_t>(sqlite3_column_bytes(m_Value, column)))
                     : std::string{};
    }

    ModelValue Value(int column, std::size_t limit) const
    {
        const int type = sqlite3_column_type(m_Value, column);
        if (type == SQLITE_NULL)
        {
            return {};
        }
        if (type == SQLITE_INTEGER)
        {
            return static_cast<std::int64_t>(sqlite3_column_int64(m_Value, column));
        }
        if (type == SQLITE_FLOAT)
        {
            const double value = sqlite3_column_double(m_Value, column);
            if (!std::isfinite(value))
            {
                throw std::runtime_error("The database contains a nonfinite real value.");
            }
            return value;
        }
        const auto count = static_cast<std::size_t>(sqlite3_column_bytes(m_Value, column));
        if (count > limit)
        {
            throw std::length_error("A database cell exceeds the adapter byte budget; its value was not truncated.");
        }
        if (type == SQLITE_TEXT)
        {
            return Text(column);
        }
        const auto* data = static_cast<const std::uint8_t*>(sqlite3_column_blob(m_Value, column));
        return count ? ModelBlob(data, data + count) : ModelBlob{};
    }
};

struct Schema
{
    ModelDataSet m_DataSet;
    std::vector<std::string> m_Names, m_Keys;
};

Schema Describe(Connection& db, const std::string& name, bool writable)
{
    Statement table(db, "SELECT type FROM main.sqlite_schema WHERE name=?1 AND type IN ('table','view')");
    table.Bind(1, name);
    if (!table.Next())
    {
        throw std::invalid_argument("The selected database table or view no longer exists.");
    }
    Schema result;
    Statement version(db, "PRAGMA main.schema_version");
    if (version.Next())
    {
        result.m_DataSet.m_SchemaRevision = static_cast<std::uint64_t>(sqlite3_column_int64(version.m_Value, 0));
    }
    result.m_DataSet.m_Id = result.m_DataSet.m_Title = name;
    result.m_DataSet.m_Kind = table.Text(0);
    bool withoutRowid = false, virtualTable = false;
    Statement traits(db, "SELECT wr,type FROM pragma_table_list WHERE schema='main' AND name=?1");
    traits.Bind(1, name);
    if (traits.Next())
    {
        withoutRowid = sqlite3_column_int(traits.m_Value, 0) != 0;
        virtualTable = traits.Text(1) != "table";
    }
    std::map<int, std::pair<std::string, bool>> primary;
    std::set<std::string> names;
    Statement columns(db, "SELECT name,type,\"notnull\",pk,hidden FROM pragma_table_xinfo(?1) ORDER BY cid");
    columns.Bind(1, name);
    while (columns.Next())
    {
        if (result.m_Names.size() >= 2048)
        {
            throw std::length_error("A database dataset accepts at most 2048 visible columns.");
        }
        const auto columnName = columns.Text(0);
        names.insert(Upper(columnName));
        const int hidden = sqlite3_column_int(columns.m_Value, 4);
        if (hidden == 1)
        {
            continue;
        }
        const int pk = sqlite3_column_int(columns.m_Value, 3);
        if (pk)
        {
            primary.emplace(pk,
                            std::make_pair(columnName, withoutRowid || sqlite3_column_int(columns.m_Value, 2) != 0));
        }
        ModelColumn column;
        column.m_Title = columnName;
        column.m_ValueType = ModelValueType::Any;
        column.m_Nullable = sqlite3_column_int(columns.m_Value, 2) == 0;
        column.m_Editable = writable && !virtualTable && result.m_DataSet.m_Kind == "table" && !hidden && !pk;
        const auto type = Upper(columns.Text(1));
        column.m_EditorType = type.find("BOOL") != std::string::npos  ? ModelValueType::Boolean
                              : type.find("INT") != std::string::npos ? ModelValueType::Integer
                              : type.find("CHAR") != std::string::npos || type.find("TEXT") != std::string::npos ||
                                      type.find("CLOB") != std::string::npos
                                  ? ModelValueType::Text
                              : type.find("BLOB") != std::string::npos || type.empty() ? ModelValueType::Blob
                                                                                       : ModelValueType::Real;
        column.m_Format = [](const ModelValue& value)
        { return value.index() ? FormatModelValue(value) : std::string("NULL"); };
        result.m_Names.push_back(columnName);
        result.m_DataSet.m_Columns.push_back(std::move(column));
    }
    if (result.m_DataSet.m_Kind == "table" && !virtualTable && !withoutRowid)
    {
        for (const auto* alias : {"_rowid_", "rowid", "oid"})
        {
            if (!names.contains(Upper(alias)))
            {
                result.m_Keys.emplace_back(alias);
                break;
            }
        }
    }
    if (result.m_Keys.empty() && result.m_DataSet.m_Kind == "table" && !virtualTable && !primary.empty() &&
        std::all_of(primary.begin(), primary.end(), [](const auto& key) { return key.second.second; }))
    {
        for (const auto& [position, key] : primary)
        {
            (void)position;
            result.m_Keys.push_back(key.first);
        }
    }
    result.m_DataSet.m_StableKeys = !result.m_Keys.empty();
    result.m_DataSet.m_Editable =
        writable && !result.m_Keys.empty() && !virtualTable && result.m_DataSet.m_Kind == "table";
    if (!result.m_DataSet.m_Editable)
    {
        for (auto& column : result.m_DataSet.m_Columns)
        {
            column.m_Editable = false;
        }
    }
    return result;
}

std::string Key(const std::vector<ModelValue>& values)
{
    std::string key;
    for (const auto& value : values)
    {
        std::string bytes;
        char type = 'n';
        if (const auto* integer = std::get_if<std::int64_t>(&value))
        {
            type = 'i';
            bytes = std::to_string(*integer);
        }
        else if (const auto* real = std::get_if<double>(&value))
        {
            type = 'r';
            bytes = FormatModelValue(*real);
        }
        else if (const auto* text = std::get_if<std::string>(&value))
        {
            type = 't';
            bytes = *text;
        }
        else if (const auto* blob = std::get_if<ModelBlob>(&value))
        {
            type = 'b';
            if (!blob->empty())
            {
                bytes.assign(reinterpret_cast<const char*>(blob->data()), blob->size());
            }
        }
        key += type;
        key += std::to_string(bytes.size());
        key += ':';
        key += bytes;
    }
    if (key.empty() || key.size() > 65536)
    {
        throw std::length_error("A database identity exceeds the bounded key budget.");
    }
    return key;
}

std::vector<ModelValue> ParseKey(std::string_view key)
{
    if (key.empty() || key.size() > 65536)
    {
        throw std::invalid_argument("Invalid source identity.");
    }
    std::vector<ModelValue> values;
    while (!key.empty())
    {
        const char type = key.front();
        key.remove_prefix(1);
        const auto colon = key.find(':');
        if (colon == std::string_view::npos || colon > 10)
        {
            throw std::invalid_argument("Invalid source identity length.");
        }
        std::size_t length = 0;
        const auto parsed = std::from_chars(key.data(), key.data() + colon, length);
        if (parsed.ec != std::errc{} || parsed.ptr != key.data() + colon)
        {
            throw std::invalid_argument("Invalid source identity length.");
        }
        key.remove_prefix(colon + 1);
        if (length > key.size() || values.size() >= 2048)
        {
            throw std::invalid_argument("Invalid source identity payload.");
        }
        const auto bytes = key.substr(0, length);
        key.remove_prefix(length);
        if (type == 'n' && bytes.empty())
        {
            values.emplace_back();
        }
        else if (type == 't')
        {
            values.emplace_back(std::string(bytes));
        }
        else if (type == 'b')
        {
            values.emplace_back(ModelBlob(bytes.begin(), bytes.end()));
        }
        else if (type == 'i' || type == 'r')
        {
            ModelColumn column;
            column.m_ValueType = type == 'i' ? ModelValueType::Integer : ModelValueType::Real;
            values.push_back(ParseModelValue(column, bytes));
        }
        else
        {
            throw std::invalid_argument("Invalid source identity type.");
        }
    }
    return values;
}

std::string FilterSql(const ModelFilter& filter, const Schema& schema, std::vector<ModelValue>& parameters)
{
    if (!filter.m_Column)
    {
        if (filter.m_Children.empty())
        {
            return filter.m_Any ? "0" : "1";
        }
        std::string sql("(");
        for (const auto& child : filter.m_Children)
        {
            if (sql.size() > 1)
            {
                sql += filter.m_Any ? " OR " : " AND ";
            }
            sql += FilterSql(child, schema, parameters);
        }
        return sql + ')';
    }
    const auto column = Quote(schema.m_Names.at(*filter.m_Column));
    const auto op = filter.m_Operator;
    const bool null = std::holds_alternative<std::monostate>(filter.m_Value);
    if (op == ModelFilterOperator::IsNull || (null && op == ModelFilterOperator::Equal))
    {
        return column + " IS NULL";
    }
    if (op == ModelFilterOperator::IsNotNull || (null && op == ModelFilterOperator::NotEqual))
    {
        return column + " IS NOT NULL";
    }
    if (null)
    {
        return "0";
    }
    parameters.push_back(filter.m_Value);
    const auto parameter = "?" + std::to_string(parameters.size());
    const bool fold = !filter.m_CaseSensitive && std::holds_alternative<std::string>(filter.m_Value);
    const auto expression = fold ? "(CASE WHEN typeof(" + column + ")='text' THEN lower(" + column + ") ELSE +" +
                                       column + " END) COLLATE BINARY"
                                 : "(+" + column + ") COLLATE BINARY";
    const auto bound = fold ? "lower(" + parameter + ")" : parameter;
    switch (op)
    {
    case ModelFilterOperator::Equal:
        return expression + " = " + bound;
    case ModelFilterOperator::NotEqual:
        return expression + " <> " + bound;
    case ModelFilterOperator::Less:
        return expression + " < " + bound;
    case ModelFilterOperator::LessEqual:
        return expression + " <= " + bound;
    case ModelFilterOperator::Greater:
        return expression + " > " + bound;
    case ModelFilterOperator::GreaterEqual:
        return expression + " >= " + bound;
    case ModelFilterOperator::Contains:
        return "(typeof(" + column + ")='text' AND instr(" + expression + "," + bound + ") > 0)";
    case ModelFilterOperator::StartsWith:
        return "(typeof(" + column + ")='text' AND instr(" + expression + "," + bound + ") = 1)";
    default:
        throw std::invalid_argument("Unsupported filter operator.");
    }
}

void BindAll(Statement& statement, const std::vector<ModelValue>& values)
{
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        statement.Bind(static_cast<int>(i + 1), values[i]);
    }
}

AdapterPage ReadPage(Connection& db, const Schema& schema, ModelQuery query, SqliteAdapterLimits limits)
{
    query.Validate(schema.m_Names.size());
    if (query.m_SchemaRevision && query.m_SchemaRevision != schema.m_DataSet.m_SchemaRevision)
    {
        throw std::runtime_error("The database schema changed; refresh the catalog before querying or editing.");
    }
    query.m_SchemaRevision = schema.m_DataSet.m_SchemaRevision;
    if (schema.m_Names.empty())
    {
        throw std::invalid_argument("The dataset has no visible columns.");
    }
    AdapterPage page;
    page.m_DataSet = schema.m_DataSet;
    std::vector<ModelValue> parameters;
    const auto filter = FilterSql(query.m_Filter, schema, parameters);
    Statement total(db, "SELECT count(*) FROM " + Quote(query.m_DataSet));
    if (total.Next())
    {
        page.m_TotalRows = static_cast<std::uint64_t>(sqlite3_column_int64(total.m_Value, 0));
    }
    Statement count(db, "SELECT count(*) FROM " + Quote(query.m_DataSet) + " WHERE " + filter);
    BindAll(count, parameters);
    if (count.Next())
    {
        page.m_MatchedRows = static_cast<std::uint64_t>(sqlite3_column_int64(count.m_Value, 0));
    }
    if (query.m_Offset >= page.m_MatchedRows)
    {
        query.m_Offset = page.m_MatchedRows ? ((page.m_MatchedRows - 1) / query.m_Limit) * query.m_Limit : 0;
    }
    page.m_Query = query;
    std::string sql("SELECT ");
    for (const auto& name : schema.m_Names)
    {
        if (sql.size() > 7)
        {
            sql += ',';
        }
        sql += Quote(name);
    }
    for (const auto& name : schema.m_Keys)
    {
        sql += ',' + Quote(name);
    }
    sql += " FROM " + Quote(query.m_DataSet) + " WHERE " + filter;
    std::string order;
    for (const auto& sort : query.m_Sort)
    {
        if (!order.empty())
        {
            order += ',';
        }
        order += Quote(schema.m_Names.at(sort.m_Column)) + (sort.m_Ascending ? " ASC" : " DESC") +
                 (sort.m_NullsLast ? " NULLS LAST" : " NULLS FIRST");
    }
    for (const auto& key : schema.m_Keys)
    {
        if (!order.empty())
        {
            order += ',';
        }
        order += Quote(key) + " ASC";
    }
    if (!order.empty())
    {
        sql += " ORDER BY " + order;
    }
    sql += " LIMIT " + std::to_string(query.m_Limit) + " OFFSET " + std::to_string(query.m_Offset);
    Statement rows(db, sql);
    BindAll(rows, parameters);
    std::size_t bytes = 0;
    while (rows.Next())
    {
        AdapterRow row;
        for (std::size_t column = 0; column < schema.m_Names.size(); ++column)
        {
            auto value = rows.Value(static_cast<int>(column), limits.m_CellBytes);
            bytes += sizeof(ModelValue);
            if (const auto* text = std::get_if<std::string>(&value))
            {
                bytes += text->size();
            }
            if (const auto* blob = std::get_if<ModelBlob>(&value))
            {
                bytes += blob->size();
            }
            if (bytes > limits.m_PageBytes)
            {
                throw std::length_error("The query page exceeds its byte budget; request fewer rows.");
            }
            row.m_Values.push_back(std::move(value));
        }
        if (schema.m_Keys.empty())
        {
            row.m_Key = "unkeyed:" + std::to_string(query.m_Offset + page.m_Rows.size());
        }
        else
        {
            std::vector<ModelValue> key;
            for (std::size_t i = 0; i < schema.m_Keys.size(); ++i)
            {
                key.push_back(rows.Value(static_cast<int>(schema.m_Names.size() + i), 65536));
            }
            row.m_Key = Key(key);
        }
        bytes += row.m_Key.size();
        if (bytes > limits.m_PageBytes)
        {
            throw std::length_error("The query page exceeds its key/value byte budget.");
        }
        page.m_Rows.push_back(std::move(row));
    }
    return page;
}

std::string StorageType(const ModelValue& value)
{
    return std::holds_alternative<std::monostate>(value) ? "null"
           : std::holds_alternative<std::string>(value)  ? "text"
           : std::holds_alternative<ModelBlob>(value)    ? "blob"
           : std::holds_alternative<double>(value)       ? "real"
                                                         : "integer";
}
} // namespace

SqliteAdapter::SqliteAdapter(std::filesystem::path path, bool writable, SqliteAdapterLimits limits)
    : m_Path(std::move(path)), m_Writable(writable), m_Limits(limits)
{
    const auto bytes = m_Path.u8string();
    m_Utf8Path.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (m_Utf8Path.empty() || m_Utf8Path.size() > 65536 || m_Utf8Path.find('\0') != std::string::npos)
    {
        throw std::invalid_argument("A database path must be bounded nonempty UTF-8 without embedded zeroes.");
    }
    if (!limits.m_CellBytes || limits.m_CellBytes > 16 * 1024 * 1024 || !limits.m_PageBytes ||
        limits.m_PageBytes > 24 * 1024 * 1024)
    {
        throw std::invalid_argument("SQLite limits require 1..16 MiB per cell and 1..24 MiB per page.");
    }
}

SqliteAdapter::~SqliteAdapter() = default;

const std::filesystem::path& SqliteAdapter::GetPath() const
{
    return m_Path;
}

bool SqliteAdapter::IsWritable() const
{
    return m_Writable;
}

std::vector<ModelDataSet> SqliteAdapter::GetDataSets(std::stop_token stop) const
{
    Connection db(m_Utf8Path, false, stop);
    db.Execute("BEGIN");
    std::vector<ModelDataSet> catalog;
    Statement tables(db, "SELECT name FROM main.sqlite_schema WHERE type IN ('table','view') AND "
                         "substr(name,1,7)<>'sqlite_' ORDER BY type,name");
    std::size_t columns = 0;
    while (tables.Next())
    {
        if (catalog.size() >= 4096)
        {
            throw std::length_error("A catalog accepts at most 4096 datasets.");
        }
        auto schema = Describe(db, tables.Text(0), m_Writable);
        columns += schema.m_Names.size();
        if (columns > 65536)
        {
            throw std::length_error("A catalog accepts at most 65536 visible columns in total.");
        }
        catalog.push_back(std::move(schema.m_DataSet));
    }
    return catalog;
}

AdapterPage SqliteAdapter::Load(const ModelQuery& query, std::stop_token stop) const
{
    query.Validate();
    Connection db(m_Utf8Path, false, stop);
    db.Execute("BEGIN");
    auto page = ReadPage(db, Describe(db, query.m_DataSet, m_Writable), query, m_Limits);
    db.Execute("COMMIT");
    return page;
}

AdapterPage SqliteAdapter::Commit(const ModelQuery& query, std::span<const AdapterEdit> edits,
                                  std::stop_token stop) const
{
    query.Validate();
    if (!m_Writable)
    {
        throw std::logic_error("Enable database editing explicitly before requesting a write.");
    }
    if (edits.empty() || edits.size() > 4096)
    {
        throw std::length_error("A database transaction accepts 1..4096 field edits.");
    }
    Connection db(m_Utf8Path, true, stop);
    db.Execute("BEGIN IMMEDIATE");
    const auto schema = Describe(db, query.m_DataSet, true);
    if (query.m_SchemaRevision && query.m_SchemaRevision != schema.m_DataSet.m_SchemaRevision)
    {
        throw std::runtime_error("The database schema changed; refresh before editing.");
    }
    if (!schema.m_DataSet.m_Editable)
    {
        throw std::logic_error("Views, virtual/unkeyed tables and generated identities are read-only.");
    }
    for (const auto& edit : edits)
    {
        const auto& column = schema.m_DataSet.m_Columns.at(edit.m_Column);
        if (!column.m_Editable)
        {
            throw std::logic_error("Primary keys and generated columns are read-only.");
        }
        ValidateModelValue(column, edit.m_Value);
        auto keys = ParseKey(edit.m_Key);
        if (keys.size() != schema.m_Keys.size())
        {
            throw std::invalid_argument("The source record identity no longer matches the table schema.");
        }
        const auto name = Quote(schema.m_Names.at(edit.m_Column));
        std::string sql = "UPDATE " + Quote(query.m_DataSet) + " SET " + name + "=?1 WHERE ";
        std::vector<ModelValue> values{edit.m_Value};
        for (std::size_t i = 0; i < keys.size(); ++i)
        {
            if (i)
            {
                sql += " AND ";
            }
            values.push_back(keys[i]);
            sql += Quote(schema.m_Keys[i]) + " IS ?" + std::to_string(values.size());
        }
        values.push_back(StorageType(edit.m_Original));
        sql += " AND typeof(" + name + ")=?" + std::to_string(values.size());
        values.push_back(edit.m_Original);
        sql += " AND " + name + " COLLATE BINARY IS ?" + std::to_string(values.size());
        Statement update(db, sql);
        BindAll(update, values);
        update.Next();
        if (sqlite3_changes64(db.m_Db) != 1)
        {
            throw std::runtime_error("The record changed or was deleted outside this view; refresh before retrying.");
        }
    }
    auto page = ReadPage(db, schema, query, m_Limits);
    db.Execute("COMMIT");
    return page;
}
} // namespace EStudioUI
