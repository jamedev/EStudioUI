#include <EStudioUI/AdapterModel.h>
#include <EStudioUI/Application.h>
#include <EStudioUI/Models.h>
#include <EStudioUI/SnapshotModelAdapter.h>
#include <EStudioUI/TableWidget.h>
#include <EStudioUI/UiContext.h>

namespace
{
struct Measurement
{
    std::int64_t m_Id;
    std::string m_Name;
    double m_Value;
};

//! A user adapter can compose existing providers and override catalog/query/write policy.
class MeasurementsAdapter final : public EStudioUI::ModelAdapter
{
  public:
    MeasurementsAdapter()
        : m_Source("measurements",
                   EStudioUI::MakeTableSnapshot<Measurement>({{1, "Pressure", 101.325}, {2, "Temperature", 293.15}},
                                                             {EStudioUI::Column("ID", &Measurement::m_Id),
                                                              EStudioUI::Column("Name", &Measurement::m_Name),
                                                              EStudioUI::Column("Value", &Measurement::m_Value)}))
    {
    }

    std::vector<EStudioUI::ModelDataSet> GetDataSets(std::stop_token stop) const override
    {
        return m_Source.GetDataSets(stop);
    }

    EStudioUI::AdapterPage Load(const EStudioUI::ModelQuery& query, std::stop_token stop) const override
    {
        return m_Source.Load(query, stop);
    }

  private:
    EStudioUI::SnapshotModelAdapter m_Source;
};
} // namespace

//! The same table consumes a user C++ provider or SqliteAdapter without database code in paint/input.
void BuildModelAdapterExample(EStudioUI::Application& application, EStudioUI::UiContext& ui)
{
    auto model = EStudioUI::AdapterModel::Create(ui, std::make_shared<MeasurementsAdapter>(), application.Tasks());
    auto& view = ui.Root().Add<EStudioUI::TableWidget>();
    view.SetModel(model);
    view.SetColumnsMovable(true);
}

int main()
{
    return 0;
}
