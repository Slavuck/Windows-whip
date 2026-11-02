# Flyout design

The settings card is 320 × 344 DIP. Its vertical sequence is a title and Training icon, whip mode, language selector, Manual row, and pause footer. Related controls share an alignment and spacing; one divider separates the pause footer. The mode selector is one 272 × 44 DIP segmented capsule, with Exact and Sadomaso inside its two halves. Selecting a half chooses that mode. The EN/RU capsule is unchanged.

Manual uses the same flat charcoal surface, rounded edges, and restrained border. The body is left aligned and uses one 19.5 DIP font size throughout. Segoe UI Variable Text is used when available, with Segoe UI as a fallback. The mode names and hotkeys use inline semibold emphasis; the final two notes are semibold. The Russian title is «Мануал». The window has no book icon, quote bars, inset cards, keycap drawings, system title bar, or scrolling text field.

DirectWrite measures every paragraph using the same font sizes, line spacing, and available widths used for painting. The window grows vertically to show the complete author-provided text. If necessary, the layout adjusts width and type size to fit the work area while keeping its portrait shape. Automated checks cover both languages at 600, 720, 900, and 1080 DIP of available height.

The Training button is an outlined target, with a 64 × 64 DIP button area in the right side of the header, with a 1.75× larger glyph and a localized hover tooltip. It opens https://test-whip.vercel.app/ in the default browser. The destination is set by the `trainingUrl` constant in `src/main.cpp`.

F8 rolls the visible lash with a smooth easing curve. The pointer is restored immediately and all strikes are disabled before the animation begins. Two points follow the cursor; the rest of the roll has inertia and sags under gravity. Mouse Raw Input wakes the frame timer when needed, and it stops after motion settles.

The layout follows Microsoft's guidance on [typography](https://learn.microsoft.com/en-us/windows/apps/design/signature-experiences/typography) and [spacing, grouping, and hierarchy](https://learn.microsoft.com/en-us/windows/apps/design/basics/content-basics). The dark surface and subtle border take visual cues from transient flyouts and Microsoft's [Acrylic guidance](https://learn.microsoft.com/en-us/windows/apps/design/style/acrylic). The current surface uses an opaque flat fill for consistent text contrast.

The selected lash design is C, with a three-pixel drawing grid and nearest-neighbor scaling. Its rigid handle is 55.25% wider than the main cable, with a seamless transition. There are no grip rings, wraps, cap ornaments, or sharp taper at the tip. A and B remain available in the developer comparison only.

The whole lash is 20% smaller than the earlier 960-pixel version. Its fixed physical length is 768 pixels. The handle is now 67.584 pixels long, a further 12% reduction from 76.8 pixels. A further 15% width increase gives an outer handle/cable width ratio of 1.5525. The free cable occupies 91.2% of the total length.

The Windows Whip title sits lower in the header, centered vertically in the space above the mode selector. No monitor previews are required by the headless verification path.
