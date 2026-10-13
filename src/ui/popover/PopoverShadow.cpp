#include "thetaexplorer/popover/PopoverShadow.h"

#include "thetaexplorer/Logger.h"

#include <QCache>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGuiApplication>
#include <QPainter>
#include <QPixmap>
#include <QScreen>

namespace PopoverShadow {
namespace {

/// Tope de memoria de la cache de sombras, en KB, compartido por todos los consumidores.
///
/// Cada sombra mide el rect + 2x`kBlurRadius` de margen, en px de DISPOSITIVO. A DPR 2 un
/// aviso chico (250x120) son ~870 KB y uno ancho ~2,3 MB, asi que 32 MB son 15-30 superficies:
/// bastante mas de las que una sesion usa de verdad.
///
/// El tope existe porque la clave es el TAMAÑO y `MessagePopover` es de tamaño variable (ancho
/// de 250 a 460 px, alto segun el texto): sin tope, cada mensaje distinto de la sesion dejaba
/// su entrada para siempre.
///
/// Bajarlo tiene un limite duro: si una sola sombra supera el tope, `QCache::insert()` borra el
/// objeto y devuelve false, y ese componente re-renderiza la sombra en CADA paint sin que nadie
/// se entere. Por eso ese fallo se loguea abajo.
constexpr int kShadowCacheBudgetKb = 32 * 1024;

/// DPR de la pantalla primaria. Sale de la pantalla y no del widget porque la primera vez que
/// se arma la sombra el popover puede no haberse mostrado todavia, y su `devicePixelRatioF()`
/// daria 1. Con monitores de densidades distintas la sombra puede quedar algo corta o larga en
/// el secundario; es preferible a que quede corta SIEMPRE.
qreal screenDpr()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    return screen ? screen->devicePixelRatio() : 1.0;
}

}  // namespace


void paintRoundedShadow(QPainter &painter, const QRectF &rect, qreal cornerRadius)
{
    if (rect.isEmpty()) {
        return;
    }

    const qreal dpr = screenDpr();
    const int margin = kBlurRadius;   // el aire que la sombra necesita alrededor del rect

    // Cache: armar la sombra cuesta un render de QGraphicsScene. La clave lleva el tamaño, el
    // radio y el DPR, que es todo lo que la cambia (los valores son constantes de compilacion).
    // Es un QCache y no un QHash porque el tamaño NO esta acotado (ver kShadowCacheBudgetKb).
    static QCache<QString, QPixmap> cache(kShadowCacheBudgetKb);
    const QSize size = rect.size().toSize();
    const QString key = QStringLiteral("%1x%2_r%3_d%4")
                            .arg(size.width()).arg(size.height())
                            .arg(cornerRadius).arg(dpr);

    QPixmap shadow;
    if (const QPixmap *cached = cache.object(key)) {
        shadow = *cached;
    }
    if (shadow.isNull()) {
        // El "objeto" que proyecta la sombra: el mismo rect redondeado, opaco.
        const QSize srcSize = size + QSize(margin * 2, margin * 2);
        QPixmap src(srcSize * dpr);
        src.setDevicePixelRatio(dpr);
        src.fill(Qt::transparent);
        {
            QPainter sp(&src);
            sp.setRenderHint(QPainter::Antialiasing);
            sp.setPen(Qt::NoPen);
            sp.setBrush(Qt::black);
            sp.drawRoundedRect(QRectF(margin, margin, size.width(), size.height()),
                               cornerRadius, cornerRadius);
        }

        // El efecto se aplica sobre un ITEM de escena y no sobre un widget: sobre una ventana
        // top-level Qt no renderiza los QGraphicsEffect.
        //
        // El BLUR y el OFFSET van en px de DISPOSITIVO (la doc de Qt lo dice del offset, y el
        // blur se comporta igual): en una Retina pedir 20 rinde 10 px logicos, y como el filtro
        // usa ~radio/2 de sigma, el alcance real caia a ~5. Por eso se escalan por el DPR.
        auto *item = new QGraphicsPixmapItem(src);
        auto *effect = new QGraphicsDropShadowEffect;
        effect->setBlurRadius(kBlurRadius * dpr);
        effect->setOffset(kOffsetX * dpr, kOffsetY * dpr);
        QColor shadowColor = kColor;
        shadowColor.setAlphaF(kOpacity);
        effect->setColor(shadowColor);
        item->setGraphicsEffect(effect);

        QGraphicsScene scene;
        scene.addItem(item);   // la escena toma la propiedad del item y del efecto

        shadow = QPixmap(srcSize * dpr);
        shadow.setDevicePixelRatio(dpr);
        shadow.fill(Qt::transparent);
        {
            QPainter rp(&shadow);
            rp.setRenderHint(QPainter::Antialiasing);
            scene.render(&rp, QRectF(QPointF(0, 0), QSizeF(srcSize)),
                         QRectF(QPointF(0, 0), QSizeF(srcSize)));
        }

        // El costo va en KB reales del pixmap, no en "1 por entrada": el tope es de memoria.
        const int costKb = qMax(1, static_cast<int>(
            (static_cast<qint64>(shadow.width()) * shadow.height() * 4) / 1024));
        if (!cache.insert(key, new QPixmap(shadow), costKb)) {
            // Una sola sombra no entra en el presupuesto: QCache borro el objeto y no cacheo
            // nada, asi que este componente va a re-renderizar su sombra en CADA paint. Es una
            // degradacion silenciosa de rendimiento; sin este aviso no hay forma de notarla.
            LOGW("popover") << "La sombra " << key << " (" << costKb
                            << " KB) no entra en el presupuesto de " << kShadowCacheBudgetKb
                            << " KB: se va a re-renderizar en cada repintado";
        }
    }

    painter.drawPixmap(QPointF(rect.left() - margin, rect.top() - margin), shadow);
}

}  // namespace PopoverShadow
