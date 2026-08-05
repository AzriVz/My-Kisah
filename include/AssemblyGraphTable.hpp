#pragma once

#include "Instruction.hpp"

#include <QTableWidget>

#include <cstddef>
#include <vector>

namespace decompiler {

struct AssemblyFlowEdge {
    int sourceRow = -1;
    int targetRow = -1;
    InstructionKind kind = InstructionKind::Normal;
    std::size_t lane = 0;
};

class AssemblyGraphTable final : public QTableWidget {
public:
    explicit AssemblyGraphTable(QWidget* parent = nullptr);

    void setFlowEdges(std::vector<AssemblyFlowEdge> edges);
    void clearFlowEdges();
    [[nodiscard]] std::size_t flowEdgeCount() const noexcept;
    [[nodiscard]] static int flowGutterWidth() noexcept;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void assignFlowLanes();

    std::vector<AssemblyFlowEdge> flowEdges_;
};

} // namespace decompiler
