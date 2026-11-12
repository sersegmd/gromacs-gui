#pragma once

#include <QDialog>

class QComboBox;
class QLineEdit;
class QLabel;

/**
 * @brief Resource estimator (Tools menu).
 *
 * Takes system size (atoms), run length (ns), integrator time step and the
 * available hardware (CPU threads, GPU) and estimates peak RAM and ns/day
 * plus the expected wall-clock time. All numbers are openly labelled as
 * heuristics calibrated for typical desktop biomolecular runs — they guide
 * planning, they are not benchmarks.
 */
class ResourceEstimatorDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ResourceEstimatorDialog(QWidget *parent = nullptr);

private slots:
    void recompute();

private:
    QLineEdit *m_atoms = nullptr;
    QLineEdit *m_ns = nullptr;
    QLineEdit *m_dt = nullptr;
    QComboBox *m_threads = nullptr;
    QComboBox *m_gpu = nullptr;
    QLabel *m_ram = nullptr;
    QLabel *m_perf = nullptr;
    QLabel *m_wall = nullptr;
    QLabel *m_note = nullptr;
};
