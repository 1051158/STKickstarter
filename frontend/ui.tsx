import Ionicons from '@expo/vector-icons/Ionicons';
import { ComponentProps, ReactNode } from 'react';
import { Pressable, ScrollView, StyleSheet, Text, TextInput, TextInputProps, View } from 'react-native';
import { colors, font, gradients, layout, radius, size, space } from './theme';

type IconName = ComponentProps<typeof Ionicons>['name'];

type ScreenProps = { children: ReactNode; nav?: ReactNode; centered?: boolean };

export function Screen({ children, nav, centered }: ScreenProps) {
  return (
    <ScrollView style={styles.page} contentContainerStyle={styles.scroll}>
      <View style={[styles.column, centered && styles.centered]}>{children}</View>
      {nav}
    </ScrollView>
  );
}

export function Title({ children, subtitle }: { children: ReactNode; subtitle?: string }) {
  return (
    <View>
      <Text style={styles.title}>{children}</Text>
      {subtitle ? <Text style={styles.subtitle}>{subtitle}</Text> : null}
    </View>
  );
}

export function Body({ children }: { children: ReactNode }) {
  return <Text style={styles.body}>{children}</Text>;
}

type ButtonProps = { label: string; onPress?: () => void; disabled?: boolean };

export function Button({ label, onPress, disabled }: ButtonProps) {
  return (
    <Pressable
      accessibilityRole="button"
      onPress={onPress}
      disabled={disabled}
      style={[styles.glass, styles.button, disabled && styles.disabled]}
    >
      <Text style={styles.buttonText}>{label}</Text>
    </Pressable>
  );
}

export function Chip({ icon, label, onPress }: { icon: IconName; label: string; onPress: () => void }) {
  return (
    <Pressable accessibilityRole="button" onPress={onPress} style={[styles.active, styles.chip]}>
      <Ionicons name={icon} size={size.iconSmall} color={colors.text} />
      <Text style={styles.label}>{label}</Text>
    </Pressable>
  );
}

export function Field({ label, ...props }: TextInputProps & { label: string }) {
  return (
    <TextInput
      accessibilityLabel={label}
      placeholder={label}
      placeholderTextColor={colors.text}
      style={[styles.glass, styles.input]}
      {...props}
    />
  );
}

export function Card({ label, children, right }: { label: string; children: ReactNode; right?: ReactNode }) {
  return (
    <View style={[styles.glass, styles.card]}>
      <View style={styles.cardBody}>
        <Text style={[styles.label, styles.secondary]}>{label}</Text>
        {children}
      </View>
      {right}
    </View>
  );
}

export function GlassCircle({ icon }: { icon: IconName }) {
  return (
    <View style={[styles.glass, styles.circle]}>
      <Ionicons name={icon} size={size.iconLarge} color={colors.text} />
    </View>
  );
}

type SegmentedProps<T extends string> = { options: readonly T[]; value: T; onChange: (value: T) => void };

export function SegmentedControl<T extends string>({ options, value, onChange }: SegmentedProps<T>) {
  return (
    <View accessibilityRole="tablist" style={[styles.glass, styles.segmented]}>
      {options.map((o) => (
        <Pressable
          key={o}
          accessibilityRole="tab"
          accessibilityState={{ selected: o === value }}
          onPress={() => onChange(o)}
          style={[styles.segment, o === value && styles.active]}
        >
          <Text style={styles.buttonText}>{o}</Text>
        </Pressable>
      ))}
    </View>
  );
}

export type Tab = 'home' | 'connect';

const tabs: { key: Tab; label: string; icon: IconName }[] = [
  { key: 'home', label: 'Home', icon: 'home' },
  { key: 'connect', label: 'Connect', icon: 'wifi' },
];

export function BottomNav({ active, onChange }: { active: Tab; onChange: (tab: Tab) => void }) {
  return (
    <View accessibilityRole="tablist" style={[styles.glass, styles.nav]}>
      {tabs.map((t) => (
        <Pressable
          key={t.key}
          accessibilityRole="tab"
          accessibilityState={{ selected: t.key === active }}
          onPress={() => onChange(t.key)}
          style={[styles.navItem, t.key === active && styles.active]}
        >
          <Ionicons name={t.icon} size={size.icon} color={colors.text} />
          <Text style={styles.navLabel}>{t.label}</Text>
        </Pressable>
      ))}
    </View>
  );
}

export function ErrorText({ message }: { message: string | null }) {
  return message ? <Text style={[styles.body, styles.error]}>{message}</Text> : null;
}

const styles = StyleSheet.create({
  page: { flex: 1, backgroundImage: gradients.background },
  scroll: { flexGrow: 1, alignItems: 'center', paddingHorizontal: space.m },
  column: {
    flexGrow: 1,
    width: '100%',
    maxWidth: size.maxWidth,
    alignItems: 'center',
    paddingTop: space.contentTop,
    gap: space.l,
  },
  centered: { justifyContent: 'center', paddingTop: 0 },
  glass: {
    backgroundColor: colors.glass,
    borderWidth: layout.borderWidth,
    borderColor: colors.glassBorder,
    borderRadius: radius.pill,
  },
  active: {
    backgroundColor: colors.glassActive,
    borderWidth: layout.borderWidth,
    borderColor: colors.glassBorder,
    borderRadius: radius.pill,
  },
  title: { ...font.title, color: colors.text, textAlign: 'center' },
  subtitle: { ...font.subtitle, color: colors.textSecondary, textAlign: 'center' },
  body: { ...font.body, color: colors.text },
  label: { ...font.label, color: colors.text },
  secondary: { color: colors.textSecondary },
  error: { color: colors.error, textAlign: 'center' },
  input: {
    ...font.body,
    color: colors.text,
    width: size.input,
    maxWidth: '100%',
    height: size.control,
    paddingHorizontal: space.inputInset,
  },
  button: {
    width: size.button,
    height: size.control,
    alignItems: 'center',
    justifyContent: 'center',
  },
  buttonText: { ...font.button, color: colors.text },
  disabled: { opacity: layout.disabledOpacity },
  chip: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: space.xs,
    paddingHorizontal: space.m,
    paddingVertical: space.s,
  },
  card: {
    flexDirection: 'row',
    alignItems: 'center',
    width: size.input,
    maxWidth: '100%',
    padding: space.card,
    borderRadius: radius.card,
  },
  cardBody: { flex: 1, gap: space.s },
  circle: {
    width: size.circle,
    height: size.circle,
    borderRadius: size.circle / 2,
    alignItems: 'center',
    justifyContent: 'center',
  },
  segmented: {
    flexDirection: 'row',
    width: size.input,
    maxWidth: '100%',
    height: size.control,
    padding: space.xs,
  },
  segment: { flex: 1, alignItems: 'center', justifyContent: 'center' },
  nav: {
    flexDirection: 'row',
    padding: space.xs,
    marginTop: space.l,
    marginBottom: space.navBottom,
  },
  navItem: {
    width: size.navItemWidth,
    height: size.navItemHeight,
    alignItems: 'center',
    justifyContent: 'center',
  },
  navLabel: { ...font.nav, color: colors.text },
});
