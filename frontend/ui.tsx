import { ReactNode } from 'react';
import { Pressable, StyleSheet, Text, TextInput, TextInputProps, View } from 'react-native';
import { colors, font, layout, space } from './theme';

export function Screen({ children }: { children: ReactNode }) {
  return (
    <View style={styles.page}>
      <View style={styles.column}>{children}</View>
    </View>
  );
}

export function Title({ children }: { children: ReactNode }) {
  return <Text style={styles.title}>{children}</Text>;
}

export function Body({ children }: { children: ReactNode }) {
  return <Text style={styles.body}>{children}</Text>;
}

type ButtonProps = { label: string; onPress: () => void; primary?: boolean; disabled?: boolean };

export function Button({ label, onPress, primary, disabled }: ButtonProps) {
  return (
    <Pressable
      accessibilityRole="button"
      onPress={onPress}
      disabled={disabled}
      style={[styles.button, primary && styles.primary, disabled && styles.disabled]}
    >
      <Text style={[styles.body, primary && styles.primaryText]}>{label}</Text>
    </Pressable>
  );
}

export function Field({ label, ...props }: TextInputProps & { label: string }) {
  return (
    <View style={styles.field}>
      <Text style={styles.body}>{label}</Text>
      <TextInput accessibilityLabel={label} style={styles.input} {...props} />
    </View>
  );
}

export function ErrorText({ message }: { message: string | null }) {
  return message ? <Text style={styles.body}>{message}</Text> : null;
}

const styles = StyleSheet.create({
  page: { flex: 1, backgroundColor: colors.background, alignItems: 'center' },
  column: { width: '100%', maxWidth: layout.maxWidth, padding: space.m, gap: space.m },
  title: { fontSize: font.title, fontWeight: 'bold', color: colors.text },
  body: { fontSize: font.body, color: colors.text },
  field: { gap: space.s },
  input: {
    borderWidth: layout.borderWidth,
    borderColor: colors.border,
    padding: space.s,
    fontSize: font.body,
    color: colors.text,
  },
  button: {
    borderWidth: layout.borderWidth,
    borderColor: colors.border,
    padding: space.m,
    alignItems: 'center',
    backgroundColor: colors.background,
  },
  primary: { backgroundColor: colors.text },
  primaryText: { color: colors.background },
  disabled: { opacity: layout.disabledOpacity },
});
