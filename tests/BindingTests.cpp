#include <EStudioUI/Bindings.h>
#include <EStudioUI/InputValidator.h>
#include <EStudioUI/Label.h>
#include <EStudioUI/NumericInput.h>
#include <EStudioUI/Slider.h>
#include <EStudioUI/ToolButton.h>

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace
{
using namespace EStudioUI;
int s_Checks = 0;

void Check(bool condition, const char* message)
{
    ++s_Checks;
    if (!condition)
        throw std::runtime_error(message);
}

template <typename Callback> void Throws(Callback callback, const char* message)
{
    bool threw = false;
    try
    {
        callback();
    }
    catch (const std::exception&)
    {
        threw = true;
    }
    Check(threw, message);
}

class MeasurementBindingProbe final : public Label
{
  public:
    std::function<void()> m_Callback;

  protected:
    Point Measure(FontFace& font) const override
    {
        if (!m_Attempted && m_Callback)
        {
            m_Attempted = true;
            m_Callback();
        }
        return Label::Measure(font);
    }

  private:
    mutable bool m_Attempted = false;
};

void TraversalPhaseBindings(FontFace& font)
{
    UiContext ui(font);
    StateStore externalStore(UiDispatcher{});
    State<std::string> external(externalStore, "original");
    auto& label = ui.Root().Add<MeasurementBindingProbe>();
    BindText(label, external);
    bool readAllowed = false;
    bool writeRejected = false;
    label.m_Callback = [&]
    {
        readAllowed = external.Get() == "original";
        try
        {
            external.Set("changed during measurement");
        }
        catch (const std::logic_error&)
        {
            writeRejected = true;
        }
    };
    ui.Prepare(400, 100);
    Check(readAllowed && writeRejected && external.Get() == "original" && label.GetText() == "original",
          "standalone state publication rejects a traversing binding target before committing");

    UiContext other(font);
    State<std::string> foreign(other.States(), "foreign original");
    auto& custom = ui.Root().Add<MeasurementBindingProbe>();
    Bind(custom, foreign, [](MeasurementBindingProbe& live, const std::string& text) { live.SetText(text); });
    bool customRejected = false;
    custom.m_Callback = [&]
    {
        try
        {
            foreign.Set("foreign changed during measurement");
        }
        catch (const std::logic_error&)
        {
            customRejected = true;
        }
    };
    ui.Prepare(400, 100);
    Check(customRejected && foreign.Get() == "foreign original" && custom.GetText() == "foreign original",
          "custom binding target phase is enforced independently of its state's other idle context");

    State<double> scalar(externalStore, 0);
    auto& slider = ui.Root().Add<Slider>();
    auto& cachedSignal = slider.ValueChanged();
    const auto scalarBinding = BindValue(slider, scalar);
    auto& emitter = ui.Root().Add<MeasurementBindingProbe>();
    emitter.m_Callback = [&] { cachedSignal.Emit(99); };
    ui.Prepare(400, 100);
    Check(scalar.Get() == 0 && slider.GetValue() == 0 && scalarBinding.GetError() &&
              !scalarBinding.IsConnected(),
          "cached control signals during traversal cannot publish or restore through a binding");
}

void ScalarAndTextBindings(FontFace& font)
{
    UiContext ui(font);
    State<double> value(ui.States(), 12);
    auto& slider = ui.Root().Add<Slider>();
    auto& number = ui.Root().Add<NumericInput>();
    BindValue(slider, value);
    BindValue(number, value);
    Check(slider.GetValue() == 12 && number.GetValue() == 12, "ignored binding handles persist and sync immediately");
    int changes = 0;
    value.Subscribe([&](const StateChange<double>&) { ++changes; });
    slider.SetValue(21);
    Check(value.Get() == 21 && number.GetValue() == 21 && changes == 1,
          "two controls share one scalar without feedback notifications");
    number.GetEditor().SetText("-");
    Check(value.Get() == 21, "intermediate numeric text stays local until a committed value exists");
    number.SetValue(32);
    Check(value.Get() == 32 && slider.GetValue() == 32 && changes == 2, "numeric committed edits update the slider");
    value.Set(41);
    Check(slider.GetValue() == 41 && number.GetValue() == 41 && changes == 3, "state propagation does not feed back");

    State<std::string> text(ui.States(), "initial");
    auto& label = ui.Root().Add<Label>("old");
    auto& editor = ui.Root().Add<TextBox>("old");
    BindText(label, text);
    BindText(editor, text);
    Check(label.GetText() == "initial" && editor.GetText() == "initial", "text bindings sync their initial value");
    editor.SetText("edited");
    Check(text.Get() == "edited" && label.GetText() == "edited", "editor and label share one string");
    auto& readout = ui.Root().Add<TextBox>();
    BindText(readout, text, BindingMode::OneWay);
    readout.SetText("local");
    Check(text.Get() == "edited", "explicit one-way text binding does not write back");
    text.Set("new");
    Check(readout.GetText() == "new", "one-way editable control still follows state commits");
}

void ValidationAndRollback(FontFace& font)
{
    UiContext ui(font);
    State<double> value(ui.States(), 10,
                        [](double candidate)
                        {
                            if (candidate > 50)
                                throw std::invalid_argument("Application limit is fifty.");
                        });
    auto& slider = ui.Root().Add<Slider>();
    auto& number = ui.Root().Add<NumericInput>();
    const auto binding = BindValue(slider, value);
    BindValue(number, value);
    int errors = 0;
    binding.SetErrorHandler([&](std::exception_ptr error) { errors += error ? 1 : 0; });
    slider.SetValue(70);
    Check(value.Get() == 10 && slider.GetValue() == 10 && number.GetValue() == 10,
          "rejected control edit restores every canonical view");
    Check(errors == 1 && binding.GetError() && binding.IsConnected(), "cancelled edit remains observable and connected");
    binding.ClearError();
    Check(!binding.GetError(), "application can acknowledge the cancelled edit");
    slider.SetValue(30);
    Check(value.Get() == 30 && slider.GetValue() == 30, "a rejected edit does not prevent later valid edits");
    Throws([&] { value.Set(70); }, "direct invalid state edits retain throwing validation behavior");
    Check(value.Get() == 30 && slider.GetValue() == 30, "direct validation fails before mutating the view");

    Throws(
        [&]
        {
            ui.States().Batch(
                [&]
                {
                    slider.SetValue(40);
                    throw std::runtime_error("Cancel application operation.");
                });
        },
        "batch body failure is observable");
    Check(value.Get() == 30 && slider.GetValue() == 30 && number.GetValue() == 30,
          "batch body failure reconciles eager bound controls");
    Throws([&] { ui.States().Batch([&] { slider.SetValue(60); }); }, "outer batch validation rejects the staged edit");
    Check(value.Get() == 30 && slider.GetValue() == 30, "batch validation rollback reconciles the editor");
    ui.States().Batch(
        [&]
        {
            value.Set(35);
            Throws(
                [&]
                {
                    ui.States().Batch(
                        [&]
                        {
                            slider.SetValue(45);
                            throw std::runtime_error("Cancel nested savepoint.");
                        });
                },
                "nested savepoint rejects its body");
            Check(value.Get() == 35 && slider.GetValue() == 35, "nested rollback restores the outer staged candidate");
        });
    Check(value.Get() == 35 && slider.GetValue() == 35 && number.GetValue() == 35,
          "outer commit retains the coherent final value");

    const auto revision = value.GetRevision();
    ui.States().Batch(
        [&]
        {
            slider.SetValue(40);
            value.Set(35);
        });
    Check(value.Get() == 35 && slider.GetValue() == 35 && number.GetValue() == 35 &&
              value.GetRevision() == revision,
          "a final no-op batch reconciles eager controls without publishing a new revision");

    State<bool> changedPeer(ui.States(), false);
    ui.States().Batch(
        [&]
        {
            slider.SetValue(45);
            value.Set(35);
            changedPeer.Set(true);
        });
    Check(value.Get() == 35 && slider.GetValue() == 35 && number.GetValue() == 35 && changedPeer.Get() &&
              value.GetRevision() == revision,
          "unchanged staged values reconcile even when a peer publishes a real change");

    State<double> unrestricted(ui.States(), 10);
    auto& limited = ui.Root().Add<Slider>();
    limited.SetRange(0, 20);
    auto rangeBinding = BindValue(limited, unrestricted);
    Throws([&] { unrestricted.Set(21); }, "bound range validation rejects unrepresentable state before commit");
    Check(unrestricted.Get() == 10 && limited.GetValue() == 10, "range rejection preserves exact canonical equality");
    rangeBinding.Disconnect();
    unrestricted.Set(21);
    Check(unrestricted.Get() == 21 && limited.GetValue() == 10, "disconnect also removes the control's range policy");

    State<std::string> text(ui.States(), "12");
    auto& editor = ui.Root().Add<TextBox>();
    editor.SetValidator(std::make_shared<IntegerValidator>(0, 99));
    editor.SetMaxBytes(2);
    BindText(editor, text);
    Throws([&] { text.Set("abc"); }, "editor policy validates canonical state before publishing");
    Throws([&] { text.Set("123"); }, "editor byte limit rejects canonical text instead of silently truncating");
    Check(text.Get() == "12" && editor.GetText() == "12", "editor representability rejection preserves exact text");
}

void ReentrantControls(FontFace& font)
{
    UiContext ui(font);
    State<double> first(ui.States(), 0);
    auto& before = ui.Root().Add<Slider>();
    before.ValueChanged().Connect([&](double value)
                                  {
                                      if (value == 1)
                                          before.SetValue(2);
                                  });
    BindValue(before, first);
    before.SetValue(1);
    Check(first.Get() == 2 && before.GetValue() == 2,
          "a stale outer control signal cannot overwrite a newer nested edit");

    State<double> second(ui.States(), 0);
    auto& after = ui.Root().Add<Slider>();
    BindValue(after, second);
    after.ValueChanged().Connect([&](double value)
                                 {
                                     if (value == 1)
                                         after.SetValue(2);
                                 });
    second.Set(1);
    Check(second.Get() == 2 && after.GetValue() == 2,
          "a reentrant control edit during propagation publishes in the next coherent round");

    State<double> initial(ui.States(), 1);
    auto& during = ui.Root().Add<Slider>();
    during.ValueChanged().Connect([&](double value)
                                  {
                                      if (value == 1)
                                          during.SetValue(2);
                                  });
    BindValue(during, initial);
    Check(initial.Get() == 2 && during.GetValue() == 2,
          "initial synchronization reconciles callback-driven newer control edits");

    State<double> minimum(ui.States(), 10,
                          [](double value)
                          {
                              if (value < 10)
                                  throw std::invalid_argument("Minimum value is ten.");
                          });
    auto& constrained = ui.Root().Add<Slider>();
    const auto constrainedBinding = BindValue(constrained, minimum);
    constrained.SetRange(0, 5);
    Check(minimum.Get() == 10 && constrained.GetValue() == 5 && !constrainedBinding.IsConnected() &&
              constrainedBinding.GetError(),
          "incompatible changed control constraints retire the binding with an inspectable error");

    State<double> rejected(ui.States(), 1,
                           [](double value)
                           {
                               if (value > 1)
                                   throw std::invalid_argument("Maximum value is one.");
                           });
    auto& recursive = ui.Root().Add<Slider>();
    const auto recursiveBinding = BindValue(recursive, rejected);
    recursive.ValueChanged().Connect([&](double value)
                                     {
                                         if (value == 1)
                                             recursive.SetValue(2);
                                     });
    recursive.SetValue(2);
    Check(rejected.Get() == 1 && recursive.GetValue() == 2 && !recursiveBinding.IsConnected() &&
              recursiveBinding.GetError(),
          "rejected edits cannot recurse indefinitely through callback-driven restoration");

    State<std::string> equality(ui.States(), "A", {},
                                [](const std::string& left, const std::string& right)
                                {
                                    return left.size() == right.size();
                                });
    auto& editor = ui.Root().Add<TextBox>();
    BindText(editor, equality);
    editor.SetText("B");
    Check(equality.Get() == "A" && editor.GetText() == "A",
          "custom no-op equality restores the exact canonical editor value");

    auto& removed = ui.Root().Add<Label>();
    const auto selfRemoving = Bind(removed, equality,
                                  [](Label& live, const std::string&)
                                  {
                                      live.GetParent()->RemoveChild(live);
                                  });
    Check(!selfRemoving.IsConnected(), "custom initial setter can destroy its receiver safely");
}

void BoolCustomAndScoped(FontFace& font)
{
    UiContext ui(font);
    State<bool> checked(ui.States(), true);
    State<bool> enabled(ui.States(), false);
    State<bool> visible(ui.States(), true);
    auto& button = ui.Root().Add<ToolButton>("toggle");
    button.SetCheckable(true);
    BindChecked(button, checked);
    BindEnabled(button, enabled);
    BindVisible(button, visible);
    Check(button.IsChecked() && !button.IsEnabled() && button.IsVisible(), "boolean property bindings sync initially");
    enabled.Set(true);
    button.SetChecked(false);
    Check(button.IsEnabled() && !checked.Get(), "checked binding publishes committed toggles");
    visible.Set(false);
    Check(!button.IsVisible(), "visibility follows canonical state");

    State<int> count(ui.States(), 7);
    auto& custom = ui.Root().Add<Label>();
    const auto persistent = Bind(custom, count, [](Label& live, const int& value) { live.SetText(std::to_string(value)); });
    Check(custom.GetText() == "7", "custom typed setter uses the same initial synchronization");
    {
        ScopedBinding scoped(persistent);
        count.Set(8);
        Check(custom.GetText() == "8", "scoped binding remains active in its owner scope");
    }
    count.Set(9);
    Check(custom.GetText() == "8" && !persistent.IsConnected(), "scoped binding disconnects on scope exit");
}

void LifetimeAndWorkers(FontFace& font)
{
    UiContext ui(font);
    State<std::string> text(ui.States(), "live");
    auto& label = ui.Root().Add<Label>();
    const auto binding = BindText(label, text);
    ui.Root().RemoveChild(label);
    Check(!binding.IsConnected(), "receiver destruction retires its binding immediately");
    text.Set("safe after deletion");
    auto& replacement = ui.Root().Add<Label>("replacement");
    text.Set("still retired");
    Check(replacement.GetText() == "replacement", "a replacement allocation cannot revive a binding");

    auto state = std::make_unique<State<std::string>>(ui.States(), "owned");
    auto& editor = ui.Root().Add<TextBox>();
    const auto editorBinding = BindText(editor, *state);
    state.reset();
    Check(!editorBinding.IsConnected(), "state destruction retires both binding directions");
    editor.SetText("safe after state deletion");

    auto& output = ui.Root().Add<Label>();
    BindText(output, text);
    const auto producer = text.GetHandle();
    DispatchReceipt receipt;
    std::thread worker([&] { receipt = producer.PostLatest("text", [](State<std::string>& live) { live.Set("worker"); }); });
    worker.join();
    Check(output.GetText() == "still retired", "worker publication stays queued before the owner boundary");
    ui.GetDispatcher().Drain();
    Check(output.GetText() == "worker" && receipt.GetStatus() == DispatchStatus::Completed,
          "worker state publication updates a live bound view on the owner");

    Binding wrongThread;
    bool rejected = false;
    std::thread invalid([&]
                        {
                            try
                            {
                                wrongThread = BindText(output, text);
                            }
                            catch (const std::logic_error&)
                            {
                                rejected = true;
                            }
                        });
    invalid.join();
    Check(rejected && !wrongThread.IsConnected(), "binding creation rejects unsupported owner access");
}
} // namespace

int main()
{
    try
    {
        FontFace font(ESTUDIOUI_FONT_PATH);
        TraversalPhaseBindings(font);
        ScalarAndTextBindings(font);
        ValidationAndRollback(font);
        ReentrantControls(font);
        BoolCustomAndScoped(font);
        LifetimeAndWorkers(font);
        std::cout << "Bindings PASS / " << s_Checks << " checks\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Bindings FAIL / " << error.what() << '\n';
        return 1;
    }
}
