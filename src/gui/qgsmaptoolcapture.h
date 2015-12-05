/***************************************************************************
    qgsmaptoolcapture.h  -  map tool for capturing points, lines, polygons
    ---------------------
    begin                : January 2006
    copyright            : (C) 2006 by Martin Dobias
    email                : wonder.sk at gmail dot com
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef QGSMAPTOOLCAPTURE_H
#define QGSMAPTOOLCAPTURE_H


#include "qgsmaptooladvanceddigitizing.h"
#include "qgscompoundcurvev2.h"
#include "qgspoint.h"
#include "qgsgeometry.h"
#include "qgslayertreeview.h"

#include <QPoint>
#include <QList>

class QgsRubberBand;
class QgsVertexMarker;
class QgsMapLayer;
class QgsGeometryValidator;

class GUI_EXPORT QgsMapToolCapture : public QgsMapToolAdvancedDigitizing
{
    Q_OBJECT

  public:
    //! constructor
    QgsMapToolCapture( QgsMapCanvas* canvas, QgsAdvancedDigitizingDockWidget* cadDockWidget, CaptureMode mode = CaptureNone );

    //! destructor
    virtual ~QgsMapToolCapture();

    //! activate the tool
    virtual void activate() override;

    //! deactive the tool
    virtual void deactivate() override;

    /** Adds a whole curve (e.g. circularstring) to the captured geometry. Curve must be in map CRS*/
    int addCurve( QgsCurveV2* c );

    /**
     * Get the capture curve
     *
     * @return Capture curve
     */
    const QgsCompoundCurveV2* captureCurve() const;


    /**
     * Update the rubberband according to mouse position
     *
     * @param e The mouse event
     */
    virtual void cadCanvasMoveEvent( QgsMapMouseEvent * e ) override;

    /**
     * Intercept key events like Esc or Del to delete the last point
     * @param e key event
     */
    virtual void keyPressEvent( QKeyEvent* e ) override;

#ifdef Q_OS_WIN
    virtual bool eventFilter( QObject *obj, QEvent *e ) override;
#endif

    /**
     * Clean a temporary rubberband
     */
    void deleteTempRubberBand();

  private slots:
    void validationFinished();
    void currentLayerChanged( QgsMapLayer *layer );
    void editingStopped();
    void addError( QgsGeometry::Error );


  protected:
    int nextPoint( const QgsPoint& mapPoint, QgsPoint& layerPoint );
    int nextPoint( const QPoint &p, QgsPoint &layerPoint, QgsPoint &mapPoint );

    /** Adds a point to the rubber band (in map coordinates) and to the capture list (in layer coordinates)
     @return 0 in case of success, 1 if current layer is not a vector layer, 2 if coordinate transformation failed*/
    int addVertex( const QgsPoint& point );

    /** Removes the last vertex from mRubberBand and mCaptureList*/
    void undo();

    /**
     * Starts capturing on the current layer. Does nothing if the capturing is already started.
     */
    void startCapturing();

    /**
     * Are we currently capturing?
     *
     * @return Is capturing for the current layer active?
     */
    bool isCapturing() const;

    /**
     * Stops the capturing. Does nothing if capturing on the given layer is not started.
     * @param vlayer layer to stop the capturing for, defaults to the current layer.
     */
    void stopCapturing( QgsVectorLayer * vlayer = 0 );

    /**
     * Number of points digitized
     *
     * @return Number of points
     */
    int size();

    /**
     * List of digitized points
     * @return List of points
     */
    QList<QgsPoint> points();

    /**
     * Set the points on which to work
     *
     * @param pointList A list of points
     */
    void setPoints( const QList<QgsPoint>& pointList );

    /**
     * Close an open polygon
     */
    void closePolygon();

  private:

    struct settingPerLayer
    {
      /** Rubber band for polylines and polygons */
      QgsRubberBand* mRubberBand;
      /** Temporary rubber band for polylines and polygons. this connects the last added point to the mouse cursor position */
      QgsRubberBand* mTempRubberBand;
      /** List to store the points of digitised lines and polygons (in layer coordinates)*/
      QgsCompoundCurveV2* mCaptureCurve;
      QStringList mValidationWarnings;
      QgsGeometryValidator *mValidator;

      QList< QgsGeometry::Error > mGeomErrors;
      QList< QgsVertexMarker * > mGeomErrorMarkers;
    };
    /** State of the tool, saved per layer */
    QHash<QgsVectorLayer*, settingPerLayer*> settingsByLayer;

    /** Return current settings */
    settingPerLayer* currentSettings() const;

    void validateGeometry();

    bool mCaptureModeFromLayer;

    QgsVertexMarker* mSnappingMarker;

    QgsVectorLayer * mCapturingLayer;

#ifdef Q_OS_WIN
    int mSkipNextContextMenuEvent;
#endif
};

#endif
