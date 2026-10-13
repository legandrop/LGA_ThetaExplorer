#pragma once

#include <QColor>

class QPainter;
class QRectF;

/**
 * @brief La sombra de TODO lo que flota en la app (popovers, avisos), en un solo lugar.
 *
 * Ninguna superficie flotante declara su propia sombra: la pide aca. Si dos cosas flotan a la
 * misma altura y proyectan sombras distintas, la interfaz se lee inconsistente.
 *
 * **Para probar valores: tocar SOLO las constantes de abajo y recompilar.**
 *
 * Los valores son los mismos que usan las demas apps LGA (ver `Doc_MessagePopover.md` del Base,
 * seccion "La sombra").
 */
namespace PopoverShadow {

// =========================================================================================
//  ⬇⬇⬇  LOS VALORES QUE SE TOCAN  ⬇⬇⬇
// =========================================================================================

/// Cuanto se DESPARRAMA la sombra, en px LOGICOS. Mas alto = mas grande y mas difusa.
/// Es aproximadamente el alcance visible, porque `paintRoundedShadow()` lo escala por el DPR
/// de la pantalla (sin ese escalado, en una Retina pedir 20 rendia ~5 px de alcance real).
/// Tambien define el margen transparente que el popover reserva alrededor, asi que subirlo
/// agranda la ventana. Rango util: 15-40.
inline constexpr int kBlurRadius = 30;

/// Cuanto TAPA la sombra, de 0.0 a 1.0. Mas alto = mas oscura y mas marcada.
/// Rango util: 0.5-0.8.
inline constexpr double kOpacity = 0.60;

/// Desplazamiento en px. **(0,0) a proposito.** Un `kOffsetY` positivo tira la sombra para
/// abajo, y sobre un popover CENTRADO en la ventana eso se lee como error: no hay un borde
/// contra el cual "flotar". El offset sirve para algo anclado a un borde.
inline constexpr int kOffsetX = 0;
inline constexpr int kOffsetY = 0;

/// Color de la sombra. Se usa el RGB; la transparencia sale de `kOpacity`.
inline const QColor kColor = QColor(0, 0, 0);

// =========================================================================================
//  ⬆⬆⬆  HASTA ACA  ⬆⬆⬆
//
//  NOTA 1: `QGraphicsDropShadowEffect` de Qt NO tiene "spread" como el `box-shadow` de CSS.
//  Los unicos parametros reales son blur, color+alpha y offset. Para que la sombra "agarre"
//  mas cerca del borde se sube la opacidad; para que llegue mas lejos, el blur.
//
//  NOTA 2: estos valores son LOGICOS. `paintRoundedShadow()` multiplica blur y offset por el
//  DPR antes de pasarlos a Qt, porque el efecto los toma en px de DISPOSITIVO. No sacar el
//  escalado sin volver a medir.
// =========================================================================================

/**
 * @brief Pinta la sombra estandar detras de un rect redondeado.
 *
 * Es el UNICO camino de esta app, a proposito. No se usa un `QGraphicsDropShadowEffect` sobre
 * el widget de contenido porque un efecto sobre un `QWidget` no cachea nada: el repintado de
 * CUALQUIER hijo (el riel de cuenta regresiva anima a ~60 Hz) re-renderiza y re-bluerea el
 * widget entero en el hilo de UI. Medido en las otras apps: 3,7 s de CPU cada 20 s con el
 * efecto contra 0,65 s con este pixmap. Ademas un efecto no se renderiza sobre una ventana
 * top-level que se pinta sola.
 *
 * NO reimplementa el blur: arma la sombra con el MISMO `QGraphicsDropShadowEffect` y las
 * constantes de arriba, renderizado a un pixmap. El pixmap se cachea por (tamaño, radio, DPR)
 * en una cache con tope de memoria.
 *
 * Quien lo usa tiene que respetar dos cosas:
 *  1. Reservar alrededor del rect un margen transparente de exactamente `kBlurRadius`: el
 *     pixmap arranca en `rect.topLeft() - kBlurRadius`. Si el margen es menor, el halo se
 *     recorta contra el borde de la ventana.
 *  2. Pasar como `cornerRadius` el mismo valor del `border-radius` del QSS de la tarjeta: el
 *     pixmap incluye el rectangulo NEGRO opaco que proyecta la sombra (el filtro de Qt dibuja
 *     la fuente encima del halo), y solo lo tapa la tarjeta si la geometria coincide.
 *
 * @param painter Painter del widget que se esta dibujando
 * @param rect Rectangulo VISIBLE (sin el margen de la sombra), en coordenadas del widget
 * @param cornerRadius Radio de las esquinas del rect visible
 */
void paintRoundedShadow(QPainter &painter, const QRectF &rect, qreal cornerRadius);

}  // namespace PopoverShadow
