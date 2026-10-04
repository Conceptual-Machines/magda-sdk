// Canvas 2D interpreter for SDK display lists (docs/display-list.md).

/** The SDK reference palette, ARGB, in ColourRole order (magda/sdk/display/ColourRole.hpp). */
export const colourRoles = [
	'background', 'surface', 'border', 'text', 'textDim',
	'accent', 'meterLow', 'meterMid', 'meterHigh', 'meterClip',
];

export const defaultPalette = {
	background: 0xFF1E1E1E,
	surface: 0xFF2A2A2A,
	border: 0xFF444444,
	text: 0xFFE0E0E0,
	textDim: 0xFF909090,
	accent: 0xFF5B9BD5,
	meterLow: 0xFF55AA55,
	meterMid: 0xFFAAAA55,
	meterHigh: 0xFFAA5555,
	meterClip: 0xFFFF3B3B,
};

const css = (argb, alpha) => {
	const a = alpha ?? ((argb >>> 24) & 0xFF) / 255;
	return `rgba(${(argb >>> 16) & 0xFF},${(argb >>> 8) & 0xFF},${argb & 0xFF},${a})`;
};

const resolveColour = (colour, palette) => {
	const argb = colour.role !== undefined
		? (palette[colour.role] ?? defaultPalette[colour.role] ?? 0xFF000000)
		: parseInt(colour.argb.slice(1), 16);
	return css(argb, colour.alpha);
};

const resolvePaint = (ctx, paint, palette) => {
	if (paint.colour) return resolveColour(paint.colour, palette);
	const { from, to, stops } = paint.linear;
	const gradient = ctx.createLinearGradient(from[0], from[1], to[0], to[1]);
	for (const [position, colour] of stops)
		gradient.addColorStop(Math.min(1, Math.max(0, position)), resolveColour(colour, palette));
	return gradient;
};

const tracePath = (ctx, path) => {
	ctx.beginPath();
	for (const [verb, ...p] of path) {
		if (verb === 'M') ctx.moveTo(p[0], p[1]);
		else if (verb === 'L') ctx.lineTo(p[0], p[1]);
		else if (verb === 'Q') ctx.quadraticCurveTo(p[0], p[1], p[2], p[3]);
		else if (verb === 'C') ctx.bezierCurveTo(p[0], p[1], p[2], p[3], p[4], p[5]);
		else if (verb === 'Z') ctx.closePath();
	}
};

const traceRect = (ctx, [x, y, w, h], radius) => {
	ctx.beginPath();
	if (radius > 0) ctx.roundRect(x, y, w, h, Math.min(radius, w / 2, h / 2));
	else ctx.rect(x, y, w, h);
};

const textAlign = { left: 'left', centre: 'center', right: 'right' };

/**
 * Draws @p list onto @p ctx at its current transform, one display unit per CSS pixel.
 * @p palette maps role names to ARGB numbers; a missing role falls back to defaultPalette.
 */
export function drawDisplayList(ctx, list, palette = defaultPalette) {
	ctx.save();
	ctx.lineJoin = 'miter';
	ctx.lineCap = 'butt';
	for (const command of list.commands) {
		switch (command.op) {
			case 'fillRect':
				ctx.fillStyle = resolvePaint(ctx, command.paint, palette);
				traceRect(ctx, command.rect, command.radius ?? 0);
				ctx.fill();
				break;
			case 'strokeRect':
				ctx.strokeStyle = resolvePaint(ctx, command.paint, palette);
				ctx.lineWidth = command.lineWidth;
				traceRect(ctx, command.rect, command.radius ?? 0);
				ctx.stroke();
				break;
			case 'fillPath':
				ctx.fillStyle = resolvePaint(ctx, command.paint, palette);
				tracePath(ctx, command.path);
				ctx.fill('nonzero');
				break;
			case 'strokePath':
				ctx.strokeStyle = resolvePaint(ctx, command.paint, palette);
				ctx.lineWidth = command.lineWidth;
				tracePath(ctx, command.path);
				ctx.stroke();
				break;
			case 'text': {
				const [x, y, w, h] = command.rect;
				ctx.save();
				ctx.beginPath();
				ctx.rect(x, y, w, h);
				ctx.clip();
				ctx.fillStyle = resolveColour(command.colour, palette);
				ctx.font = `${command.fontSize}px sans-serif`;
				ctx.textBaseline = 'middle';
				ctx.textAlign = textAlign[command.justification] ?? 'left';
				const anchorX = command.justification === 'right' ? x + w
					: command.justification === 'centre' ? x + w / 2 : x;
				ctx.fillText(command.text, anchorX, y + h / 2);
				ctx.restore();
				break;
			}
			case 'clipRect': {
				// Whole pixels, outward, as the JUCE interpreter clips.
				const [x, y, w, h] = command.rect;
				const left = Math.floor(x);
				const top = Math.floor(y);
				ctx.beginPath();
				ctx.rect(left, top, Math.ceil(x + w) - left, Math.ceil(y + h) - top);
				ctx.clip();
				break;
			}
			case 'save':
				ctx.save();
				break;
			case 'restore':
				ctx.restore();
				break;
		}
	}
	ctx.restore();
}
