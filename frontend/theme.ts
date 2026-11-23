import { TextStyle } from 'react-native';

export const colors = {
  text: '#FFFFFF',
  textSecondary: '#A4A4A4',
  error: '#FFB4B4',
  glass: 'rgba(251, 252, 253, 0.16)',
  glassActive: 'rgba(251, 252, 253, 0.26)',
  glassBorder: 'rgba(255, 255, 255, 0.2)',
  focusBorder: 'rgba(255, 255, 255, 0.7)',
};

// CSS gradients, passed through as `backgroundImage` by react-native-web.
export const gradients = {
  background: [
    'radial-gradient(70% 35% at 0% 38%, rgba(30, 60, 230, 0.9) 0%, rgba(30, 60, 230, 0) 100%)',
    'radial-gradient(60% 35% at 100% 42%, rgba(115, 8, 200, 0.9) 0%, rgba(115, 8, 200, 0) 100%)',
    'radial-gradient(80% 30% at 50% 0%, rgba(70, 10, 150, 0.8) 0%, rgba(70, 10, 150, 0) 100%)',
    'linear-gradient(180deg, #160c5c 0%, #2a2590 45%, #0b0a4c 78%, #070939 100%)',
  ].join(', '),
};

export const space = {
  xs: 4,
  s: 8,
  m: 16,
  card: 20,
  inputInset: 27,
  l: 32,
  contentTop: 112,
  navBottom: 44,
};

export const radius = {
  pill: 32,
  card: 24,
};

export const size = {
  maxWidth: 393,
  input: 320,
  control: 58,
  button: 175,
  circle: 150,
  navItemWidth: 80,
  navItemHeight: 46,
  iconSmall: 14,
  icon: 20,
  iconLarge: 64,
};

const family = '-apple-system, BlinkMacSystemFont, "SF Pro Text", system-ui, sans-serif';

export const font = {
  title: { fontFamily: family, fontSize: 40, lineHeight: 48, fontWeight: '400' },
  subtitle: { fontFamily: family, fontSize: 14, lineHeight: 17, fontWeight: '400' },
  body: { fontFamily: family, fontSize: 16, lineHeight: 19, fontWeight: '400' },
  button: { fontFamily: family, fontSize: 16, lineHeight: 19, fontWeight: '600' },
  label: { fontFamily: family, fontSize: 12, lineHeight: 15, fontWeight: '400' },
  nav: { fontFamily: family, fontSize: 12, lineHeight: 15, fontWeight: '300' },
  pin: { fontFamily: family, fontSize: 28, lineHeight: 34, fontWeight: '700' },
} satisfies Record<string, TextStyle>;

export const layout = {
  borderWidth: 1,
  disabledOpacity: 0.4,
};
