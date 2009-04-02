/**********************************************************************
  VibrationPlot - Visualize vibrational modes graphically

  Copyright (C) 2009 by David Lonie

  This file is part of the Avogadro molecular editor project.
  For more information, see <http://avogadro.openmolecules.net/>

  This library is free software; you can redistribute it and/or modify
  it under the terms of the GNU Library General Public License as
  published by the Free Software Foundation; either version 2.1 of the
  License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
 ***********************************************************************/

#include "vibrationplot.h"

#include <QPen>
#include <QColor>
#include <QColorDialog>
#include <QButtonGroup>
#include <QDebug>
#include <QDoubleValidator>
#include <QFileDialog>
#include <QFontDialog>
#include <QMessageBox>
#include <QFile>
#include <QDir>
#include <QPixmap>

#include <avogadro/molecule.h>
#include <avogadro/plotwidget.h>
#include <openbabel/mol.h>
#include <openbabel/generic.h>

using namespace OpenBabel;
using namespace std;

namespace Avogadro {

  VibrationPlot::VibrationPlot( QWidget *parent, Qt::WindowFlags f ) : 
      QDialog( parent, f )
  {
    ui.setupUi(this);
    
    //TODO link the scale here to vibrationdialog
    m_scale = 1.0;
    ui.spin_scale->setValue(m_scale);

    m_yaxis = ui.combo_yaxis->currentText();

    // Hide advanced options initially
    ui.gb_customize->hide();

    // setting the limits for the plot
    ui.plot->setFontSize( 10);
    ui.plot->setDefaultLimits( 4000.0, 400.0, 0.0, 100.0 );
    ui.plot->setAntialiasing(true);
    ui.plot->axis(PlotWidget::BottomAxis)->setLabel(tr("Wavenumber (cm<sup>-1</sup>)"));
    ui.plot->axis(PlotWidget::LeftAxis)->setLabel(m_yaxis);
    m_calculatedSpectra = new PlotObject (Qt::red, PlotObject::Lines, 2);
    m_importedSpectra = new PlotObject (Qt::white, PlotObject::Lines, 2);
    m_nullSpectra = new PlotObject (Qt::white, PlotObject::Lines, 2); // Used to replace disabled plot objects
    ui.plot->addPlotObject(m_calculatedSpectra);
    ui.plot->addPlotObject(m_importedSpectra);

    connect(ui.push_colorBackground, SIGNAL(clicked()),
            this, SLOT(changeBackgroundColor()));
    connect(ui.push_colorForeground, SIGNAL(clicked()),
            this, SLOT(changeForegroundColor()));
    connect(ui.push_colorCalculated, SIGNAL(clicked()),
            this, SLOT(changeCalculatedSpectraColor()));
    connect(ui.push_colorImported, SIGNAL(clicked()),
            this, SLOT(changeImportedSpectraColor()));
    connect(ui.push_font, SIGNAL(clicked()),
            this, SLOT(changeFont()));
    connect(ui.push_customize, SIGNAL(clicked()),
            this, SLOT(toggleCustomize()));
    connect(ui.push_save, SIGNAL(clicked()),
            this, SLOT(saveImage()));
    connect(ui.cb_import, SIGNAL(toggled(bool)),
            this, SLOT(toggleImported(bool)));
    connect(ui.cb_calculate, SIGNAL(toggled(bool)),
            this, SLOT(toggleCalculated(bool)));
    connect(ui.cb_labelPeaks, SIGNAL(toggled(bool)),
            this, SLOT(regenerateCalculatedSpectra()));
    connect(ui.push_import, SIGNAL(clicked()),
            this, SLOT(importSpectra()));
    connect(this, SIGNAL(scaleUpdated()),
            this, SLOT(regenerateCalculatedSpectra()));
    connect(ui.spin_scale, SIGNAL(valueChanged(double)),
            this, SLOT(setScale(double)));
    connect(ui.spin_FWHM, SIGNAL(valueChanged(double)),
            this, SLOT(regenerateCalculatedSpectra()));
    connect(ui.combo_yaxis, SIGNAL(currentIndexChanged(QString)),
            this, SLOT(updateYAxis(QString)));
  }

  VibrationPlot::~VibrationPlot()
  {
    //TODO: Anything to delete?
  }

  void VibrationPlot::updateYAxis(QString text)
  {
    if (m_yaxis == ui.combo_yaxis->currentText()) {
      return;
    }
    ui.plot->axis(PlotWidget::LeftAxis)->setLabel(text);
    m_yaxis = text;
    regenerateCalculatedSpectra();
    regenerateImportedSpectra();
  }

  void VibrationPlot::changeBackgroundColor()
  {
    //TODO: Store color choices in config?
    QColor current (ui.plot->backgroundColor());
    QColor color = QColorDialog::getColor(current, this);//, tr("Select Background Color")); <-- Title not supported until Qt 4.5 bump.
    if (color.isValid() && color != current) {
      ui.plot->setBackgroundColor(color);
      updatePlot();
    }
  }

  void VibrationPlot::changeForegroundColor()
  {
    //TODO: Store color choices in config?
    QColor current (ui.plot->foregroundColor());
    QColor color = QColorDialog::getColor(current, this);//, tr("Select Foreground Color")); <-- Title not supported until Qt 4.5 bump.
    if (color.isValid() && color != current) {
      ui.plot->setForegroundColor(color);
      updatePlot();
    }
  }

  void VibrationPlot::changeCalculatedSpectraColor()
  {
    //TODO: Store color choices in config?
    QPen currentPen = m_calculatedSpectra->linePen();
    QColor current (currentPen.color());
    QColor color = QColorDialog::getColor(current, this);//, tr("Select Calculated Spectra Color")); <-- Title not supported until Qt 4.5 bump.
    if (color.isValid() && color != current) {
      currentPen.setColor(color);
      m_calculatedSpectra->setLinePen(currentPen);
      updatePlot();
    }
  }


  void VibrationPlot::changeImportedSpectraColor()
  {
    //TODO: Store color choices in config?
    QPen currentPen (m_importedSpectra->linePen());
    QColor current (currentPen.color());
    QColor color = QColorDialog::getColor(current, this);//, tr("Select Imported Spectra Color")); <-- Title not supported until Qt 4.5 bump.
    if (color.isValid() && color != current) {
      currentPen.setColor(color);
      m_importedSpectra->setLinePen(currentPen);
      updatePlot();
    }
  }

  void VibrationPlot::changeFont()
  {
    bool ok;
    QFont current (ui.plot->getFont());
    QFont font = QFontDialog::getFont(&ok, current, this);
    if (ok) {
      ui.plot->setFont(font);
      updatePlot();
    } 
  }

  void VibrationPlot::setMolecule(Molecule *molecule)
  {
    m_molecule = molecule;
    OBMol obmol = m_molecule->OBMol();

    // Get intensities
    m_vibrations = static_cast<OBVibrationData*>(obmol.GetData(OBGenericDataType::VibrationData));
    if (!m_vibrations) {
      qWarning() << "VibrationPlot::setMolecule: No vibrations to plot!";
      return;
    }

    // OK, we have valid vibrations, so store them
    m_wavenumbers = m_vibrations->GetFrequencies();
    vector<double> intensities = m_vibrations->GetIntensities();

    // FIXME: dlonie: remove this when OB is fixed!! Hack to get around bug in how open babel reads in QChem files
    // While openbabel is broken, remove indicies (n+3), where
    // n=0,1,2...
    if (m_wavenumbers.size() == 0.75 * intensities.size()) {
      uint count = 0;
      for (uint i = 0; i < intensities.size(); i++) {
        if ((i+count)%3 == 0){
          intensities.erase(intensities.begin()+i);
          count++;
          i--;
        }
      }
    }

    // Normalize intensities into transmittances
    double maxIntensity=0;
    for (unsigned int i = 0; i < intensities.size(); i++) {
      if (intensities.at(i) >= maxIntensity) {
        maxIntensity = intensities.at(i);
      }
    }

    if (maxIntensity == 0) {
      qWarning() << "VibrationPlot::setMolecule: No intensities > 0 in dataset.";
      return;
    }

    for (unsigned int i = 0; i < intensities.size(); i++) {
      double t = intensities.at(i);
      t = t / maxIntensity; 	// Normalize
      t = 0.97 * t;		// Keeps the peaks from extending to the limits of the plot
      t = 1 - t; 		// Simulate transmittance
      t *= 100;			// Convert to percent
      m_transmittances.push_back(t);
    }

    regenerateCalculatedSpectra();
  }

  void VibrationPlot::setScale(double scale)
  {
    if (scale == m_scale) {
      return;
    }
    m_scale = scale;
    emit scaleUpdated();
  }

  void VibrationPlot::importSpectra()
  {
    QFileInfo defaultFile(m_molecule->fileName());
    QString defaultPath = defaultFile.canonicalPath();
    if (defaultPath.isEmpty())
      defaultPath = QDir::homePath();

    QString defaultFileName = defaultPath + '/' + defaultFile.baseName() + ".tsv";
    QString filename 	= QFileDialog::getOpenFileName(this, tr("Import Spectra"), defaultFileName, tr("Tab Separated Values (*.tsv);;Text Files (*.txt);;All Files (*.*)"));

    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
      qDebug() << "Error reading file " << filename;
      return;
    }
    
    // Clear out any old data
    m_imported_wavenumbers.clear();
    m_imported_transmittances.clear();

    QTextStream in(&file);
    // Process each line
    while (!in.atEnd()) {
      QString line = in.readLine();

      // the following assumes that the file is a tsv of wavenumber \t transmittance
      QStringList data = line.split("\t");
      if (data.at(0).toDouble() && data.at(1).toDouble()) {
        m_imported_wavenumbers.push_back(data.at(0).toDouble());
        m_imported_transmittances.push_back(data.at(1).toDouble());
      }
      else {
        qDebug() << "VibrationPlot::importSpectra Skipping entry as invalid:\n\tWavenumber: " << data.at(0) << "\n\tTransmittance: " << data.at(1);
      }
    }

    // Check to see if the transmittances are in fractions or percents by looking for any transmittances > 1.5
    bool convert = true;
    for (uint i = 0; i < m_imported_transmittances.size(); i++) {
      if (m_imported_transmittances.at(i) > 1.5) { // If transmittances exist greater than this, they're already in percent.
        convert = false;
        break;
      }
    }
    if (convert) {
      for (uint i = 0; i < m_imported_transmittances.size(); i++) {
        m_imported_transmittances.at(i) *= 100;
      }
    }

    ui.push_colorImported->setEnabled(true);
    ui.cb_import->setEnabled(true);
    ui.cb_import->setChecked(true);
    getImportedSpectra(m_importedSpectra);
    updatePlot();
  }

  void VibrationPlot::saveImage()
  {
    QFileInfo defaultFile(m_molecule->fileName());
    QString defaultPath = defaultFile.canonicalPath();
    if (defaultPath.isEmpty())
      defaultPath = QDir::homePath();

    QString defaultFileName = defaultPath + '/' + defaultFile.baseName() + ".png";
    QString filename 	= QFileDialog::getSaveFileName(this, tr("Save Spectra"), defaultFileName, tr("png (*.png);;jpg (*.jpg);;bmp (*.bmp);;tiff (*.tiff);;All Files (*.*)"));
    QPixmap pix = QPixmap::grabWidget(ui.plot);
    if (!pix.save(filename)) {
      qWarning() << "VibrationPlot::saveImage Error saving plot to " << filename;
    }
  }

  void VibrationPlot::toggleImported(bool state) {
    if (state) {
      ui.plot->replacePlotObject(1,m_importedSpectra);
    }
    else {
      ui.plot->replacePlotObject(1,m_nullSpectra);
    }
    updatePlot();
  }

  void VibrationPlot::toggleCalculated(bool state) {
    if (state) {
      ui.plot->replacePlotObject(0,m_importedSpectra);
    }
    else {
      ui.plot->replacePlotObject(0,m_calculatedSpectra);
    }
    updatePlot();
  }

  void VibrationPlot::toggleCustomize() {
    if (ui.gb_customize->isHidden()) {
      ui.push_customize->setText(tr("Customi&ze <<"));
      ui.gb_customize->show();
    }
    else {
      ui.push_customize->setText(tr("Customi&ze >>"));
      ui.gb_customize->hide();
    }
  }

  void VibrationPlot::regenerateCalculatedSpectra() {
    getCalculatedSpectra(m_calculatedSpectra);
    updatePlot();
  }

  void VibrationPlot::regenerateImportedSpectra() {
    getImportedSpectra(m_importedSpectra);
    updatePlot();
  }

  void VibrationPlot::updatePlot()
  {
    ui.plot->update();
  }

  void VibrationPlot::getCalculatedSpectra(PlotObject *vibrationPlotObject)
  {
    vibrationPlotObject->clearPoints();
    if (ui.spin_FWHM->value() == 0.0) {
      getCalculatedSinglets(vibrationPlotObject);
    } 
    else {
      getCalculatedGaussians(vibrationPlotObject);
    }
    if (ui.combo_yaxis->currentText() == "Absorbance (%)") {
      for(int i = 0; i< vibrationPlotObject->points().size(); i++) {
        double absorbance = 100 - vibrationPlotObject->points().at(i)->y();
        vibrationPlotObject->points().at(i)->setY(absorbance);
      }
    }
  }

  void VibrationPlot::getCalculatedSinglets(PlotObject *vibrationPlotObject)
  {
    vibrationPlotObject->addPoint( 400, 100); // Initial point

    for (uint i = 0; i < m_transmittances.size(); i++) {
      double wavenumber = m_wavenumbers.at(i) * m_scale;
      double transmittance = m_transmittances.at(i);
      vibrationPlotObject->addPoint ( wavenumber, 100 );
      if (ui.cb_labelPeaks->isChecked()) {
        vibrationPlotObject->addPoint ( wavenumber, transmittance, QString::number(wavenumber, 'f', 1));
      }
      else {
       	vibrationPlotObject->addPoint ( wavenumber, transmittance );
      }
      vibrationPlotObject->addPoint ( wavenumber, 100 );
    }
    vibrationPlotObject->addPoint( 4000, 100); // Final point
  }


  void VibrationPlot::getCalculatedGaussians(PlotObject *vibrationPlotObject)
  {
    // convert FWHM to sigma squared
    double FWHM = ui.spin_FWHM->value();
    double s2	= pow( (FWHM / ( 2 * sqrt( 2 * log(2) ) ) ), 2);
    
    // determine range
    // - find maximum and minimum
    double min = 0.0 + 2*FWHM;
    double max = 4000.0 - 2*FWHM;
    for (uint i = 0; i < m_wavenumbers.size(); i++) {
      double cur = m_wavenumbers.at(i);
      if (cur > max) max = cur;
      if (cur < min) min = cur;
    }
    min -= 2*FWHM;
    max += 2*FWHM;      
    // - get resolution (TODO)
    double res = 1.0;
    // create points
    for (double x = min; x < max; x += res) {
      double y = 100;
      for (uint i = 0; i < m_transmittances.size(); i++) {
        double t = m_transmittances.at(i);
        double w = m_wavenumbers.at(i) * m_scale;
        y += (t-100) * exp( - ( pow( (x - w), 2 ) ) / (2 * s2) );
      }
      vibrationPlotObject->addPoint(x,y);
    }

    // Normalization is probably screwed up, so renormalize the data
    max = vibrationPlotObject->points().at(0)->y();
    min = max;
    for(int i = 0; i< vibrationPlotObject->points().size(); i++) {
      double cur = vibrationPlotObject->points().at(i)->y();
      if (cur < min) min = cur;
      if (cur > max) max = cur;
    }
    for(int i = 0; i< vibrationPlotObject->points().size(); i++) {
      double cur = vibrationPlotObject->points().at(i)->y();
      // cur - min 		: Shift lowest point of plot to be at zero
      // 100 / (max - min)	: Conversion factor for current spread -> percent
      // * 0.97 + 3		: makes plot stay away from 0 transmittance 
      //			: (easier to see multiple peaks on strong signals)
      vibrationPlotObject->points().at(i)->setY( (cur - min) * 100 / (max - min) * 0.97 + 3);
    }    
  }

  void VibrationPlot::getImportedSpectra(PlotObject *vibrationPlotObject)
  {
    vibrationPlotObject->clearPoints();
    // For now, lets just make singlet peaks. Maybe we can fit a
    // gaussian later?
    for (uint i = 0; i < m_imported_transmittances.size(); i++) {
      double wavenumber = m_imported_wavenumbers.at(i);
      double y = m_imported_transmittances.at(i);
      if (ui.combo_yaxis->currentText() == "Absorbance (%)") {
        y = 100 - y;
      }
      vibrationPlotObject->addPoint ( wavenumber, y );
    }
  }
}

#include "vibrationplot.moc"