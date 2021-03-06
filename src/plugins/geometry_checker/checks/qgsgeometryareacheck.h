/***************************************************************************
 *  qgsgeometryareacheck.h                                                 *
 *  -------------------                                                    *
 *  copyright            : (C) 2014 by Sandro Mani / Sourcepole AG         *
 *  email                : smani@sourcepole.ch                             *
 ***************************************************************************/

#ifndef QGS_GEOMETRY_AREA_CHECK_H
#define QGS_GEOMETRY_AREA_CHECK_H

#include "qgsgeometrycheck.h"

class QgsSurfaceV2;

class QgsGeometryAreaCheck : public QgsGeometryCheck
{
    Q_OBJECT

  public:
    QgsGeometryAreaCheck( QgsFeaturePool* featurePool, double threshold )
        : QgsGeometryCheck( FeatureCheck, featurePool ), mThreshold( threshold ) {}
    void collectErrors( QList<QgsGeometryCheckError*>& errors, QStringList& messages, QAtomicInt* progressCounter = nullptr, const QgsFeatureIds& ids = QgsFeatureIds() ) const override;
    void fixError( QgsGeometryCheckError* error, int method, int mergeAttributeIndex, Changes& changes ) const override;
    const QStringList& getResolutionMethods() const override;
    QString errorDescription() const override { return tr( "Minimal area" ); }
    QString errorName() const override { return "QgsGeometryAreaCheck"; }
  private:
    enum ResolutionMethod { MergeLongestEdge, MergeLargestArea, MergeIdenticalAttribute, Delete, NoChange };

    bool mergeWithNeighbor( QgsFeature &feature, int partIdx, int method, int mergeAttributeIndex, Changes &changes , QString &errMsg ) const;
    virtual bool checkThreshold( const QgsAbstractGeometryV2* geom, double& value ) const { value = geom->area(); return value < mThreshold; }

  protected:
    double mThreshold;
};

#endif // QGS_GEOMETRY_AREA_CHECK_H
