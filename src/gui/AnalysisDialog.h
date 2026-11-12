#pragma once

#include <QComboBox>
#include <QDialog>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QWidget>
#include <QVector>

class QLabel;
class QPushButton;

#include "core/CoreBridge.h"

/**
 * @brief Lightweight XVG plotter (custom-painted, no extra Qt modules).
 *
 * Draws axes, ticks, a grid, one coloured polyline per series and a legend.
 */
class XvgPlotWidget : public QWidget
{
    Q_OBJECT

public:
    explicit XvgPlotWidget(QWidget *parent = nullptr);

    void setXvgData(const XvgData &data);
    void clearPlot();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    XvgData m_data;
};

/**
 * @brief Analysis window: computes energy and trajectory reports from the
 * simulation output and plots the resulting curves.
 *
 * The dialog discovers the produced data files (energy, run input,
 * trajectory) in the project directory itself — the user never types file
 * names. A run is only offered when the files the chosen report needs are
 * actually present, so stale or missing inputs cannot produce failures.
 */
class AnalysisDialog : public QDialog
{
    Q_OBJECT

public:
    //! Data sources each report type requires.
    enum DataNeeds { NeedsEnergy = 1, NeedsRunInput = 2, NeedsTrajectory = 4 };

    AnalysisDialog(CoreBridge *core, const QString &projectDir, const QString &gmxPath,
                   QWidget *parent = nullptr);

signals:
    void commandReady(const QString &label, const QString &command, const QString &workdir,
                      const QString &stdinData, const QString &xvgToPlot);

public slots:
    //! Called when the analysis job finished; loads the newest .xvg in the dir.
    void jobCompleted(const QString &xvgHint);

private slots:
    void presetChanged(int index);
    void openExistingXvg();
    void loadGroupChoices();
    void compareXvgFiles();
    void exportPlotPdf();

private:
    //! Re-scans the project directory for energy / run-input / trajectory files.
    void rescanDataFiles();
    //! Enables or blocks Run depending on the current preset's data needs.
    void updateRunState();
    //! Builds the interactive selection text sent to the running report.
    QString buildStdinText() const;

    CoreBridge *m_core = nullptr;
    QString m_dir;
    QString m_gmx;

    QComboBox *m_preset = nullptr;
    QComboBox *m_energyFile = nullptr;
    QComboBox *m_runInput = nullptr;
    QComboBox *m_trajectory = nullptr;
    QLabel *m_dataStatus = nullptr;
    QPlainTextEdit *m_selection = nullptr;
    XvgPlotWidget *m_plot = nullptr;
    QComboBox *m_groupA = nullptr;
    QComboBox *m_groupB = nullptr;
    QPushButton *m_runButton = nullptr;
};
