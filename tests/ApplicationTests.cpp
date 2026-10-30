#include "TestSupport.h"
#include <val/application/SettingsService.h>
#include <val/application/WorkspaceService.h>
#include <type_traits>

namespace
{
using namespace val::application;

/** Deterministic settings port with observable calls and injectable load/save outcomes. */
class TestSettingsStore final : public SettingsStore
{
public:
    SettingsLoadResult next{SettingsLoadState::missing, {}, {}};
    Result saved{Result::success()};
    int saves{};
    SettingsLoadResult load() override { return next; }
    Result save(const Settings&) override { ++saves; return saved; }
};

/** Atomic counter command whose failures exercise history transitions without changing its target. */
class CounterCommand final : public Command
{
public:
    CounterCommand(int& value, bool& failExecute, bool& failUndo)
        : value_(value), failExecute_(failExecute), failUndo_(failUndo) {}
    std::string_view name() const noexcept override { return "Increment"; }
    Result execute() override
    {
        if (failExecute_)
            return Result::failure("Injected execute failure.");
        ++value_;
        return Result::success();
    }
    Result undo() override
    {
        if (failUndo_)
            return Result::failure("Injected undo failure.");
        --value_;
        return Result::success();
    }
private:
    int& value_;
    bool& failExecute_;
    bool& failUndo_;
};

void testHistory()
{
    CommandHistory history(2);
    int value{};
    bool failExecute{}, failUndo{};
    auto next = [&] { return std::make_unique<CounterCommand>(value, failExecute, failUndo); };
    require(!history.undo().succeeded && !history.redo().succeeded, "Empty history must fail explicitly.");
    require(!history.execute(nullptr).succeeded, "Null command must fail.");
    require(history.execute(next()).succeeded && value == 1, "Execute must mutate.");
    failUndo = true;
    require(!history.undo().succeeded && history.canUndo() && value == 1, "Failed undo preserves cursor.");
    failUndo = false;
    require(history.undo().succeeded && value == 0 && history.canRedo(), "Undo exposes redo.");
    failExecute = true;
    require(!history.redo().succeeded && history.canRedo(), "Failed redo preserves cursor.");
    require(!history.execute(next()).succeeded && history.canRedo(), "Failed new command retains redo.");
    failExecute = false;
    require(history.redo().succeeded && value == 1, "Redo executes command.");
    require(history.execute(next()).succeeded && history.execute(next()).succeeded && value == 3,
            "Capacity does not prevent new edits.");
    require(history.undo().succeeded && history.undo().succeeded && value == 1,
            "Capacity preserves newest undo entries.");
    require(!history.undo().succeeded, "Oldest entry is evicted.");
    require(history.execute(next()).succeeded && !history.canRedo(), "New branch discards redo.");
    require(history.undoName() == "Increment", "History exposes command names.");
    history.clear();
    require(!history.canUndo() && !history.canRedo(), "Clear resets both directions.");
    bool threw{};
    try { CommandHistory invalid(0); } catch (const std::invalid_argument&) { threw = true; }
    require(threw, "Zero capacity must be rejected.");
}

void testSettings()
{
    TestSettingsStore store;
    SettingsService service(store);
    require(!service.save({}).succeeded && store.saves == 0, "Writes require load first.");
    require(service.load().succeeded && service.current() == Settings{}, "Missing file chooses explicit defaults.");
    Settings edited{1200, 800, false};
    require(service.save(edited).succeeded && service.current() == edited, "Valid save is accepted.");
    require(!service.save({10, 10, true}).succeeded && service.current() == edited, "Invalid values preserve settings.");
    store.saved = Result::failure("Injected write failure.");
    require(!service.save({1300, 900, true}).succeeded && service.current() == edited,
            "Failed save preserves accepted preferences.");
    store.next = {SettingsLoadState::failed, {}, "Injected load failure."};
    require(!service.load().succeeded && service.current() == edited, "Failed load preserves preferences.");
    const auto writes = store.saves;
    require(!service.save(edited).succeeded && writes == store.saves, "Failed load inhibits overwrites.");
    store.next = {SettingsLoadState::loaded, {1, 1, true}, {}};
    require(!service.load().succeeded, "Adapter values must also pass application validation.");
    store.next = {SettingsLoadState::loaded, edited, {}};
    store.saved = Result::success();
    require(service.load().succeeded && service.save(edited).succeeded, "Valid reload restores writes.");
}

void testWorkspace()
{
    static_assert(!std::is_move_constructible_v<WorkspaceService>);
    static_assert(!std::is_copy_constructible_v<WorkspaceService>);
    WorkspaceService service;
    require(!service.rename("").succeeded && !service.rename(" \t").succeeded, "Reject blank workspace identity.");
    require(!service.rename("Injected\nline").succeeded, "Reject control characters.");
    require(service.rename("Study").succeeded && service.workspace().name == "Study", "Rename use case works.");
    require(service.undo().succeeded && service.workspace().name == "Untitled", "Workspace undo works.");
    require(service.redo().succeeded && service.workspace().name == "Study", "Workspace redo works.");
    require(service.rename("Study").succeeded, "Same name is a no-op.");
    require(service.undo().succeeded && service.workspace().name == "Untitled", "No-op creates no history entry.");
}
}

int main()
{
    return runTests([] { testHistory(); testSettings(); testWorkspace(); });
}
