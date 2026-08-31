import { DynamoDBClient } from "@aws-sdk/client-dynamodb";
import { DynamoDBDocumentClient, PutCommand } from "@aws-sdk/lib-dynamodb";
import { SNSClient, PublishCommand } from "@aws-sdk/client-sns";

const ddbClient = new DynamoDBClient({});
const ddb = DynamoDBDocumentClient.from(ddbClient);
const sns = new SNSClient({});

const TABLE_NAME = process.env.TABLE_NAME || "EngineTelemetry";
const SNS_TOPIC_ARN = process.env.SNS_TOPIC_ARN;
const VIBRATION_THRESHOLD = parseFloat(process.env.VIBRATION_THRESHOLD || "15");
const TEMP_MAX_C = parseFloat(process.env.TEMP_MAX_C || "35");
const TEMP_MIN_C = parseFloat(process.env.TEMP_MIN_C || "-5");
const TEMP_CALIBRATION_OFFSET_C = parseFloat(process.env.TEMP_CALIBRATION_OFFSET_C || "0");

export const handler = async (event) => {
  console.log("Incoming telemetry:", JSON.stringify(event));

  const {
    device_id,
    timestamp,
    humidity,
    vibration_x,
    vibration_y,
    vibration_z,
  } = event;

  // Sensor calibration: the ESP32's DHT22 reads ~3C higher than the factory's
  // reference thermometer (verified on-site). Corrected here so DynamoDB,
  // threshold alerting, and Grafana all reflect the true temperature.
  const temperature = event.temperature + TEMP_CALIBRATION_OFFSET_C;

  // 1. Persist the reading in DynamoDB
  await ddb.send(new PutCommand({
    TableName: TABLE_NAME,
    Item: { device_id, timestamp, temperature, humidity, vibration_x, vibration_y, vibration_z },
  }));

  // 2. Evaluate alert conditions
  const absoluteMotion = Math.abs(vibration_x) + Math.abs(vibration_y);
  const reasons = [];

  if (absoluteMotion > VIBRATION_THRESHOLD) {
    reasons.push(
      `Abnormal vibration detected (|x|+|y| = ${absoluteMotion.toFixed(2)}, threshold ${VIBRATION_THRESHOLD})`
    );
  }
  if (temperature > TEMP_MAX_C) {
    reasons.push(`Temperature too high: ${temperature}°C (max ${TEMP_MAX_C}°C)`);
  }
  if (temperature < TEMP_MIN_C) {
    reasons.push(`Temperature too low: ${temperature}°C (min ${TEMP_MIN_C}°C)`);
  }

  // 3. Publish an alert if any threshold was breached
  if (reasons.length > 0 && SNS_TOPIC_ARN) {
    const message =
      `Device: ${device_id}\n` +
      `Time: ${new Date(timestamp).toISOString()}\n\n` +
      `Alert(s):\n- ${reasons.join("\n- ")}\n\n` +
      `Readings: temp=${temperature}°C, humidity=${humidity}%, ` +
      `vibration=(${vibration_x}, ${vibration_y}, ${vibration_z})`;

    await sns.send(new PublishCommand({
      TopicArn: SNS_TOPIC_ARN,
      Subject: `Kalaneuvos Engine Alert: ${device_id}`,
      Message: message,
    }));

    console.log("Alert published to SNS:", reasons);
  }

  return { statusCode: 200, alerted: reasons.length > 0, reasons };
};
