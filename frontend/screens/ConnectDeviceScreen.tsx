import { Button, GlassCircle, Title } from '../ui';

// Bluetooth pairing is not built yet, so the button stays disabled.
export default function ConnectDeviceScreen() {
  return (
    <>
      <GlassCircle icon="wifi" />
      <Title subtitle="Turn on Bluetooth and keep your device nearby">Connect</Title>
      <Button label="Connect device" disabled />
    </>
  );
}
