#include "TestSupport.h"
#include <val/application/WorkspaceService.h>
#include <limits>

int main()
{
    return runTests([]
    {
        using namespace val;
        using domain::Port;
        application::WorkspaceService service;
        require(service.createNode({0, 0}, "A").succeeded, "Create A.");
        const auto a = service.lastCreatedNode();
        require(service.createNode({300, 0}, "B").succeeded, "Create B.");
        const auto b = service.lastCreatedNode();
        require(service.createNode({600, 0}, "C").succeeded, "Create C.");
        const auto c = service.lastCreatedNode();
        require(service.connect({a, Port::output}, {b, Port::input}).succeeded, "Connect A to B.");
        require(service.connect({b, Port::output}, {c, Port::input}).succeeded, "Connect B to C.");
        const auto baseline = service.graph();
        for (auto endpoints : {
                 std::pair{domain::Endpoint{a, Port::input}, domain::Endpoint{b, Port::input}},
                 std::pair{domain::Endpoint{a, Port::output}, domain::Endpoint{b, Port::output}},
                 std::pair{domain::Endpoint{0, Port::output}, domain::Endpoint{b, Port::input}},
                 std::pair{domain::Endpoint{a, Port::output}, domain::Endpoint{a, Port::input}},
                 std::pair{domain::Endpoint{a, Port::output}, domain::Endpoint{b, Port::input}},
                 std::pair{domain::Endpoint{a, Port::output}, domain::Endpoint{c, Port::input}},
                 std::pair{domain::Endpoint{c, Port::output}, domain::Endpoint{a, Port::input}}})
        {
            require(!service.connect(endpoints.first, endpoints.second).succeeded,
                    "Reject wrong direction, missing/self/duplicate/occupied/cyclic endpoints.");
            require(service.graph() == baseline, "Rejected connections preserve all graph content.");
        }
        require(service.moveNodes({{a, {16, 20}}, {b, {316, 20}}}).succeeded, "Move selection once.");
        require(service.undo().succeeded && service.graph() == baseline, "One undo restores all moved nodes.");
        require(service.canRedo(), "Move remains available to redo.");
        require(!service.moveNodes({{a, {1, 2}}, {9999, {1, 2}}}).succeeded
                && service.graph() == baseline && service.canRedo(), "Partial invalid moves are atomic.");
        require(!service.createNode({std::numeric_limits<double>::quiet_NaN(), 0}).succeeded,
                "Reject non-finite placement.");
        require(!service.moveNodes({{a, {1000001, 0}}}).succeeded, "Reject out-of-range placement.");
        require(!service.disconnect(9999).succeeded && service.canRedo(), "Invalid deletes retain redo.");
        require(service.moveNodes({{a, baseline.nodes.at(a).position}}).succeeded && service.canRedo(),
                "No-op movement does not consume history or truncate redo.");
        require(service.redo().succeeded, "Redo selection movement.");
        const auto moved = service.graph();
        require(service.deleteNodes({a, b, b}).succeeded
                && service.graph().nodes.size() == 1 && service.graph().connections.empty(),
                "Delete deduplicates selected nodes and removes every incident wire.");
        require(service.undo().succeeded && service.graph() == moved,
                "Undo deletion restores exact node identities, geometry and incident wires.");
        require(service.redo().succeeded && service.undo().succeeded && service.graph() == moved,
                "Deletion supports repeated exact undo/redo.");
        const auto wire = service.graph().connections.begin()->first;
        require(service.disconnect(wire).succeeded && service.graph().connections.size() == 1,
                "Disconnect only the specified wire.");
        require(service.undo().succeeded && service.graph() == moved, "Undo reconnects the original identity.");
        require(service.redo().succeeded && service.undo().succeeded, "Redo disconnection is stable.");
        require(service.rename("Shared history").succeeded && !service.canRedo(),
                "Workspace identity and graph use one journal and new edits truncate redo.");
        require(service.undo().succeeded && service.workspace().name == "Untitled"
                && service.graph() == moved, "Undo rename does not mutate graph.");
        require(service.undo().succeeded && service.graph() == baseline, "Next undo reaches the graph move.");

        application::WorkspaceService branching;
        require(branching.createNode({0, 0}).succeeded, "Create first identity.");
        const auto first = branching.lastCreatedNode();
        require(branching.undo().succeeded && branching.createNode({1, 1}).succeeded,
                "Create on a new branch.");
        require(branching.lastCreatedNode() > first && !branching.canRedo(), "Identities are never recycled.");
        require(branching.createNode({2, 2}).succeeded, "Create another endpoint.");
        const auto second = branching.lastCreatedNode();
        require(branching.connect({first + 1, Port::output}, {second, Port::input}).succeeded,
                "Output can connect to an input.");
        require(branching.createNode({3, 3}).succeeded, "Create fan-out destination.");
        require(branching.connect({first + 1, Port::output},
                                  {branching.lastCreatedNode(), Port::input}).succeeded,
                "Output fan-out is allowed.");

        application::WorkspaceService journal;
        std::vector<domain::Graph> snapshots{journal.graph()};
        require(journal.createNode({-1, -2}, "First").succeeded, "Round-trip create first.");
        const auto firstId = journal.lastCreatedNode();
        snapshots.push_back(journal.graph());
        require(journal.createNode({300, 10}, "Second").succeeded, "Round-trip create second.");
        const auto secondId = journal.lastCreatedNode();
        snapshots.push_back(journal.graph());
        require(journal.connect({firstId, Port::output}, {secondId, Port::input}).succeeded, "Round-trip wire.");
        snapshots.push_back(journal.graph());
        require(journal.moveNodes({{firstId, {70, 80}}, {secondId, {400, 40}}}).succeeded, "Round-trip move.");
        snapshots.push_back(journal.graph());
        require(journal.disconnect(journal.graph().connections.begin()->first).succeeded, "Round-trip disconnect.");
        snapshots.push_back(journal.graph());
        require(journal.deleteNodes({firstId, secondId}).succeeded, "Round-trip delete.");
        snapshots.push_back(journal.graph());
        for (std::size_t step = snapshots.size() - 1; step > 0; --step)
            require(journal.undo().succeeded && journal.graph() == snapshots[step - 1],
                    "Every content command restores its exact preceding snapshot.");
        require(!journal.canUndo(), "Undo reaches the original empty graph.");
        for (std::size_t step = 1; step < snapshots.size(); ++step)
            require(journal.redo().succeeded && journal.graph() == snapshots[step],
                    "Every content command redoes its exact following snapshot.");
        require(!journal.canRedo(), "Redo reaches the accepted final graph.");
    });
}
