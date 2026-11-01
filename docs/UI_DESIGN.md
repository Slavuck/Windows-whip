# Flyout design

The settings card is 360 × 444 DIP. Its vertical sequence is a title and Training icon, whip mode, language selector, Manual row, and pause footer. Related controls share an alignment and spacing; thin dividers separate the groups. The green mode switch and EN/RU capsule keep the reference shapes.

Manual uses the same dark blue-green surface, rounded edges, restrained border, and vector book icon. The body is left aligned. Segoe UI Variable Text is used when available, with Segoe UI as a fallback. Semibold lead text, normal body text, inset mode explanations, keyboard labels, and smaller notes create a readable hierarchy. It has no system title bar or scrolling text field.

DirectWrite measures every paragraph using the same font sizes, line spacing, and available widths used for painting. The window grows vertically to show the complete author-provided text. If necessary, the layout adjusts width and type size to fit the work area while keeping its portrait shape. Automated checks cover both languages at 600, 720, 900, and 1080 DIP of available height.

The Training button is an outlined target, with a 44 × 44 DIP hit area and a localized hover tooltip. It opens https://test-whip.vercel.app/ in the default browser. The destination is set by the `trainingUrl` constant in `src/main.cpp`.

F8 turns the visible lash into a resting spiral using a smooth easing curve. The pointer is restored immediately and all strikes are disabled before the animation begins. The resting circle does not follow the mouse or keep rendering frames.

The layout follows Microsoft's guidance on [typography](https://learn.microsoft.com/en-us/windows/apps/design/signature-experiences/typography) and [spacing, grouping, and hierarchy](https://learn.microsoft.com/en-us/windows/apps/design/basics/content-basics). The dark surface and subtle border take visual cues from transient flyouts and Microsoft's [Acrylic guidance](https://learn.microsoft.com/en-us/windows/apps/design/style/acrylic). The current vector surface uses an opaque gradient for consistent text contrast.
