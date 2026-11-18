import { Body, Button, Screen, Title } from '../ui';

export default function ConnectDeviceScreen({ onBack }: { onBack: () => void }) {
  return (
    <Screen>
      <Button label="Back" onPress={onBack} />
      <Title>Connect device</Title>
      <Body>Coming soon</Body>
    </Screen>
  );
}
